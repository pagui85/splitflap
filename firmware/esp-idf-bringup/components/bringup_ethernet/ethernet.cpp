// SPDX-License-Identifier: Apache-2.0
#include "bringup_ethernet.hpp"

#include <atomic>
#include <cinttypes>
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "soc/emac_periph.h"

namespace bringup::ethernet {
namespace {
constexpr char kTag[] = "bringup_eth";
constexpr int kPhyAddr = CONFIG_BRINGUP_ETH_PHY_ADDR;
constexpr int kReset = CONFIG_BRINGUP_ETH_PHY_RESET;
constexpr int kClockOut = CONFIG_BRINGUP_ETH_CLK_OUT;
constexpr int kClockIn = CONFIG_BRINGUP_ETH_CLK_IN;

using PhyFactory = esp_eth_phy_t *(*)(const eth_phy_config_t *);
constexpr struct {
    const char *name;
    PhyFactory make;
} kPhys[] = {{"unset", nullptr}, {"LAN87xx", esp_eth_phy_new_lan87xx},
            {"RTL8201", esp_eth_phy_new_rtl8201}, {"IP101", esp_eth_phy_new_ip101},
            {"KSZ80xx", esp_eth_phy_new_ksz80xx}, {"DP83848", esp_eth_phy_new_dp83848}};
constexpr auto kPhy = kPhys[CONFIG_BRINGUP_ETH_PHY];
const auto &mux = emac_rmii_iomux_pins;
const struct Pin {
    const char *signal;
    int gpio;
    const emac_iomux_info_t *iomux;
    bool output;
    bool optional;
} pins[] = {
    {"MDC", CONFIG_BRINGUP_ETH_MDC, nullptr, true, false},
    {"MDIO", CONFIG_BRINGUP_ETH_MDIO, nullptr, true, false},
    {"RESET_n", kReset, nullptr, true, true},
    {"REF_CLK_IN", kClockIn, mux.clki, false, false},
    {"REF_CLK_OUT", kClockOut, mux.clko, true, true},
    {"TX_EN", CONFIG_BRINGUP_ETH_TX_EN, mux.tx_en, true, false},
    {"TXD0", CONFIG_BRINGUP_ETH_TXD0, mux.txd0, true, false},
    {"TXD1", CONFIG_BRINGUP_ETH_TXD1, mux.txd1, true, false},
    {"CRS_DV", CONFIG_BRINGUP_ETH_CRS_DV, mux.crs_dv, false, false},
    {"RXD0", CONFIG_BRINGUP_ETH_RXD0, mux.rxd0, false, false},
    {"RXD1", CONFIG_BRINGUP_ETH_RXD1, mux.rxd1, false, false},
};

esp_eth_handle_t driver = nullptr;
esp_netif_t *netif = nullptr;
esp_eth_netif_glue_handle_t glue = nullptr;
esp_event_handler_instance_t eth_handler = nullptr;
esp_event_handler_instance_t ip_handler = nullptr;
esp_event_handler_instance_t lost_ip_handler = nullptr;
httpd_handle_t server = nullptr;
esp_err_t startup_result = ESP_ERR_INVALID_STATE;

// All writers are on the default event loop. Readers include the HTTP task.
enum class Link { Down, Up, DhcpReady };
std::atomic<Link> link{Link::Down};

bool valid_mux(const Pin &pin)
{
    if (!pin.iomux) return true;
    for (auto choice = pin.iomux; choice->gpio_num != GPIO_NUM_MAX; ++choice) {
        if (choice->gpio_num == pin.gpio) return true;
    }
    return false;
}

esp_err_t validate_pins()
{
    if (CONFIG_BRINGUP_ETH_PHY == 0 || kPhyAddr < 0 || kReset < -1) {
        ESP_LOGE(kTag, "Set the PHY model, address and reset wiring before enabling Ethernet");
        return ESP_ERR_INVALID_ARG;
    }
    if (kClockOut < -1) {
        ESP_LOGE(kTag, "Set the RMII clock output GPIO or select external clock with -1");
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t assigned = 0;
    for (const auto &pin : pins) {
        if (pin.gpio == -1 && pin.optional) continue;
        // Reserve both console paths. Other strapping pins can be documented
        // RMII data pins, so check the schematic and reset levels before use.
        if (!GPIO_IS_VALID_GPIO(pin.gpio) || (pin.output && !GPIO_IS_VALID_OUTPUT_GPIO(pin.gpio)) ||
            pin.gpio == 24 || pin.gpio == 25 || pin.gpio == 37 || pin.gpio == 38 || !valid_mux(pin)) {
            ESP_LOGE(kTag, "%s has missing, reserved or unsupported GPIO %d", pin.signal, pin.gpio);
            return ESP_ERR_INVALID_ARG;
        }
        const uint64_t bit = UINT64_C(1) << pin.gpio;
        if (assigned & bit) {
            ESP_LOGE(kTag, "%s duplicates GPIO %d", pin.signal, pin.gpio);
            return ESP_ERR_INVALID_ARG;
        }
        assigned |= bit;
    }
    return ESP_OK;
}

struct Status {
    bool connected = false;
    bool ready = false;
    unsigned speed_mbps = 0;
    uint8_t mac[6]{};
    esp_netif_ip_info_t ip{};
    esp_ip4_addr_t dns[2]{};
};

Status read_status()
{
    Status status;
    if (!driver || !netif) return status;
    esp_eth_ioctl(driver, ETH_CMD_G_MAC_ADDR, status.mac);
    const auto state = link.load();
    status.connected = state != Link::Down;
    if (status.connected) {
        eth_speed_t speed;
        if (esp_eth_ioctl(driver, ETH_CMD_G_SPEED, &speed) == ESP_OK)
            status.speed_mbps = speed == ETH_SPEED_100M ? 100 : 10;
    }
    if (state == Link::DhcpReady && esp_netif_get_ip_info(netif, &status.ip) == ESP_OK &&
        status.ip.ip.addr != 0 && link.load() == Link::DhcpReady) {
        status.ready = true;
        constexpr esp_netif_dns_type_t types[] = {ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP};
        for (unsigned i = 0; i < 2; ++i) {
            esp_netif_dns_info_t dns{};
            if (esp_netif_get_dns_info(netif, types[i], &dns) == ESP_OK && dns.ip.type == ESP_IPADDR_TYPE_V4)
                status.dns[i] = dns.ip.u_addr.ip4;
        }
    } else {
        status.ip = {};
    }
    return status;
}

void on_eth(void *, esp_event_base_t, int32_t event, void *data)
{
    if (*static_cast<esp_eth_handle_t *>(data) != driver) return;
    switch (event) {
    case ETHERNET_EVENT_CONNECTED:
        link.store(Link::Up);
        ESP_LOGI(kTag, "link up; waiting for DHCP");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
    case ETHERNET_EVENT_STOP:
        link.store(Link::Down);
        ESP_LOGI(kTag, "link down; DHCP status cleared");
        break;
    default:
        break;
    }
}

void on_ip(void *, esp_event_base_t, int32_t event, void *data)
{
    if (event == IP_EVENT_ETH_GOT_IP) {
        const auto &lease = *static_cast<const ip_event_got_ip_t *>(data);
        if (lease.esp_netif != netif || link.load() == Link::Down) return;
        link.store(Link::DhcpReady);
        ESP_LOGI(kTag, "DHCP IPv4 " IPSTR " gateway " IPSTR " health=http://" IPSTR "/health",
                 IP2STR(&lease.ip_info.ip), IP2STR(&lease.ip_info.gw), IP2STR(&lease.ip_info.ip));
    } else if (event == IP_EVENT_ETH_LOST_IP && link.load() == Link::DhcpReady) {
        link.store(Link::Up);
        ESP_LOGI(kTag, "DHCP lease lost; waiting for IPv4");
    }
}

esp_err_t health(httpd_req_t *request)
{
    const Status status = read_status();
    const esp_app_desc_t &app = *esp_app_get_description();
    char json[512];
    const int size = std::snprintf(json, sizeof(json),
        "{\"firmware\":\"%s\",\"version\":\"%s\",\"link\":%s,\"dhcp_ready\":%s,"
        "\"speed_mbps\":%u,\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
        "\"ip\":\"" IPSTR "\",\"gateway\":\"" IPSTR "\",\"dns\":[\"" IPSTR "\",\"" IPSTR "\"]}\n",
        app.project_name, app.version, status.connected ? "true" : "false", status.ready ? "true" : "false",
        status.speed_mbps, status.mac[0], status.mac[1], status.mac[2], status.mac[3], status.mac[4], status.mac[5],
        IP2STR(&status.ip.ip), IP2STR(&status.ip.gw), IP2STR(&status.dns[0]), IP2STR(&status.dns[1]));
    if (size < 0 || static_cast<unsigned>(size) >= sizeof(json))
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "status unavailable");
    httpd_resp_set_type(request, "application/json");
    if (!status.ready) httpd_resp_set_status(request, "503 Service Unavailable");
    return httpd_resp_send(request, json, size);
}

void start_http()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    esp_err_t result = httpd_start(&server, &config);
    if (result == ESP_OK) {
        httpd_uri_t route{};
        route.uri = "/health";
        route.method = HTTP_GET;
        route.handler = health;
        result = httpd_register_uri_handler(server, &route);
        if (result != ESP_OK) {
            httpd_stop(server);
            server = nullptr;
        }
    }
    if (result != ESP_OK) ESP_LOGE(kTag, "HTTP health unavailable: %s", esp_err_to_name(result));
}

esp_err_t attach_and_start()
{
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    netif = esp_netif_new(&netif_config);
    if (!netif) return ESP_ERR_NO_MEM;
    glue = esp_eth_new_netif_glue(driver);
    if (!glue) return ESP_ERR_NO_MEM;
    esp_err_t result = esp_netif_attach(netif, glue);
    if (result != ESP_OK) return result;
    result = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth, nullptr, &eth_handler);
    if (result != ESP_OK) return result;
    result = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip, nullptr, &ip_handler);
    if (result != ESP_OK) return result;
    result = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, on_ip, nullptr, &lost_ip_handler);
    if (result != ESP_OK) return result;
    result = esp_eth_start(driver);
    // IDF changes the driver's state to START before negotiation can fail.
    if (result != ESP_OK) esp_eth_stop(driver);
    return result;
}

esp_err_t configure()
{
    esp_err_t result = validate_pins();
    if (result != ESP_OK) return result;
    result = esp_netif_init();
    if (result != ESP_OK) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK) return result;

    auto emac = eth_esp32_emac_config_t{};
    emac.smi_gpio = {CONFIG_BRINGUP_ETH_MDC, CONFIG_BRINGUP_ETH_MDIO};
    emac.interface = EMAC_DATA_INTERFACE_RMII;
    emac.dma_burst_len = ETH_DMA_BURST_LEN_32;
    emac.emac_dataif_gpio.rmii = {CONFIG_BRINGUP_ETH_TX_EN, CONFIG_BRINGUP_ETH_TXD0, CONFIG_BRINGUP_ETH_TXD1,
                                 CONFIG_BRINGUP_ETH_CRS_DV, CONFIG_BRINGUP_ETH_RXD0, CONFIG_BRINGUP_ETH_RXD1};
    emac.clock_config.rmii = {EMAC_CLK_EXT_IN, kClockIn};
    emac.clock_config_out_in.rmii = {EMAC_CLK_EXT_IN, -1};
    if (kClockOut >= 0) {
        emac.clock_config.rmii = {EMAC_CLK_OUT, kClockOut};
        emac.clock_config_out_in.rmii = {EMAC_CLK_EXT_IN, kClockIn};
    }
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    auto *mac = esp_eth_mac_new_esp32(&emac, &mac_config);
    if (!mac) return ESP_ERR_NO_MEM;
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = kPhyAddr;
    phy_config.reset_gpio_num = kReset;
    auto *phy = kPhy.make(&phy_config);
    if (!phy) {
        mac->del(mac);
        return ESP_ERR_NO_MEM;
    }
    esp_eth_config_t eth = ETH_DEFAULT_CONFIG(mac, phy);
    result = esp_eth_driver_install(&eth, &driver);
    if (result == ESP_OK) result = attach_and_start();
    if (result == ESP_OK) return ESP_OK;

    if (lost_ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_ETH_LOST_IP, lost_ip_handler);
    if (ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, ip_handler);
    if (eth_handler) esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_handler);
    link.store(Link::Down);
    if (glue) esp_eth_del_netif_glue(glue);
    if (netif) {
        esp_netif_destroy(netif);
        netif = nullptr;
    }
    if (driver) {
        const esp_err_t removed = esp_eth_driver_uninstall(driver);
        if (removed != ESP_OK) {
            ESP_LOGE(kTag, "Ethernet driver cleanup failed: %s", esp_err_to_name(removed));
            return result;
        }
        driver = nullptr;
    }
    phy->del(phy);
    mac->del(mac);
    return result;
}
} // namespace

bool uses_pin(int gpio)
{
    if (gpio < 0) return false;
    for (const auto &pin : pins) {
        if (pin.gpio == gpio) return true;
    }
    return false;
}

void start()
{
    startup_result = configure();
    if (startup_result == ESP_OK) start_http();
    else ESP_LOGE(kTag, "Ethernet startup failed: %s; standalone console available", esp_err_to_name(startup_result));
}

void print_status()
{
    const Status status = read_status();
    std::printf("ethernet=enabled startup=%s phy=%s address=%d rmii_clock=%s dhcp=on http=%s\n",
                esp_err_to_name(startup_result), kPhy.name, kPhyAddr, kClockOut < 0 ? "external_input" : "p4_output_loopback",
                server ? "/health" : "off");
    for (const auto &pin : pins) std::printf("ethernet_pin_%s=%d\n", pin.signal, pin.gpio);
    std::printf("link=%s speed_mbps=%u mac=%02x:%02x:%02x:%02x:%02x:%02x dhcp_ready=%s\n",
                status.connected ? "up" : "down", status.speed_mbps, status.mac[0], status.mac[1],
                status.mac[2], status.mac[3], status.mac[4], status.mac[5], status.ready ? "yes" : "no");
    std::printf("ip=" IPSTR " gateway=" IPSTR " dns_primary=" IPSTR " dns_backup=" IPSTR "\n",
                IP2STR(&status.ip.ip), IP2STR(&status.ip.gw), IP2STR(&status.dns[0]), IP2STR(&status.dns[1]));
}
}
