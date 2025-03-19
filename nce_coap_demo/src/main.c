/**
 * @file coap_client.c
 * @brief 1NCE CoAP client implementation for Nordic Semiconductor devices.
 *
 * This file contains the implementation of a CoAP client that communicates over UDP,
 * with optional DTLS support. It supports uplink and downlink operations.
 *
 */

#include <stdio.h>
#include <string.h>

#if defined(CONFIG_POSIX_API)
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/poll.h>
#else
#include <zephyr/net/socket.h>
#endif /* CONFIG_POSIX_API */

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/random/random.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include "nce_iot_c_sdk.h"
#include <network_interface_zephyr.h>

LOG_MODULE_REGISTER(coap_client_sample, CONFIG_COAP_CLIENT_SAMPLE_LOG_LEVEL);

#if defined(CONFIG_NCE_ENABLE_DTLS)
#include <modem/modem_key_mgmt.h>
#include <nrf_modem_at.h>
#include <zephyr/net/tls_credentials.h>
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

/** @brief Event masks for Zephyr NET management events. */
#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define CONN_LAYER_EVENT_MASK (NET_EVENT_CONN_IF_FATAL_ERROR)

/* Function prototypes */
void uplink_work_fn(struct k_work *work);

/** @brief Work queue items for uplink. */
K_WORK_DELAYABLE_DEFINE(uplink_work, uplink_work_fn);

/** @brief Macro for handling fatal errors by rebooting the device. */
#define FATAL_ERROR()                              \
	LOG_ERR("Fatal error! Rebooting the device."); \
	LOG_PANIC();                                   \
	IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)))

/** @brief Network management event callback structures. */
static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback conn_cb;
static bool is_connected; /**< Indicates if network is connected. */

/** @brief Mutex and conditional variable for network connectivity signaling. */
K_MUTEX_DEFINE(network_connected_lock);
K_CONDVAR_DEFINE(network_connected);

/** @brief Construct CoAP URI path with configurable query parameter. */
#define CONFIG_URI_PATH "/?" CONFIG_COAP_URI_QUERY
static int sock = -1;
#if defined(CONFIG_NCE_ENABLE_DEVICE_CONTROLLER)
/** @brief Mutex to prevent conflicts on socket operations. */
K_MUTEX_DEFINE(socket_mutex);
void downlink_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(downlink_work, downlink_work_fn);
static int recv_fd = -1;
#define COAP_CODE_CLASS_SIZE 32
#define COAP_SUCCESS_CODE_CLASS 2
#endif
#if defined(CONFIG_NCE_ENABLE_DTLS)
/** @brief Security tag for DTLS. */
const sec_tag_t tls_sec_tag[] = {
	CONFIG_NCE_DTLS_SECURITY_TAG,
};
DtlsKey_t nceKey = {0};
extern void sys_arch_reboot(int type);
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

static int server_resolve(struct sockaddr_storage *server)
{
	int err;
	struct addrinfo *result;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM};
	char ipv4_addr[NET_IPV4_ADDR_LEN];

	err = getaddrinfo(CONFIG_COAP_SAMPLE_SERVER_HOSTNAME, NULL, &hints, &result);
	if (err)
	{
		LOG_ERR("getaddrinfo, error: %d", err);
		return err;
	}

	if (result == NULL)
	{
		LOG_ERR("Address not found");
		return -ENOENT;
	}

	/* IPv4 Address. */
	struct sockaddr_in *server4 = ((struct sockaddr_in *)server);

	server4->sin_addr.s_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	server4->sin_family = AF_INET;
	server4->sin_port = htons(CONFIG_COAP_SAMPLE_SERVER_PORT);

	inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr, sizeof(ipv4_addr));

	LOG_INF("IPv4 Address found %s", ipv4_addr);

	/* Free the address. */
	freeaddrinfo(result);

	return 0;
}
/** @brief Waits for network connectivity before proceeding. */
static void wait_for_network(void)
{
	k_mutex_lock(&network_connected_lock, K_FOREVER);

	if (!is_connected)
	{
		LOG_INF("Waiting for network connectivity");
		k_condvar_wait(&network_connected, &network_connected_lock, K_FOREVER);
	}

	k_mutex_unlock(&network_connected_lock);
}

static void response_cb(int16_t code, size_t offset, const uint8_t *payload,
						size_t len, bool last_block, void *user_data)
{
	if (code >= 0)
	{
		LOG_INF("CoAP response: code: 0x%x", code);
	}
	else
	{
		LOG_INF("Response received with error code: %d", code);
	}
}
#if defined(CONFIG_NCE_ENABLE_DTLS)
/* Store DTLS Credentials in the modem */
int store_credentials(void)
{
	int err;
	char psk_hex[100];
	int cred_len;

	/* Convert PSK to HEX */
	cred_len = bin2hex(nceKey.Psk, strlen(nceKey.Psk), psk_hex, sizeof(psk_hex));

	if (cred_len == 0)
	{
		LOG_ERR("PSK is too large to convert (%d)", -EOVERFLOW);
		return -EOVERFLOW;
	}

	/* Store DTLS Credentials */
	err = modem_key_mgmt_write(CONFIG_NCE_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, psk_hex, sizeof(psk_hex));
	LOG_DBG("psk status: %d\n", err);
	LOG_INF("psk: %s\n", nceKey.Psk);

	err = modem_key_mgmt_write(CONFIG_NCE_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY, nceKey.PskIdentity, sizeof(nceKey.PskIdentity));
	LOG_DBG("psk_id status: %d\n", err);
	LOG_INF("PskIdentity: %s\n", nceKey.PskIdentity);

	return err;
}

/**
 * @brief Onboard the device by managing DTLS credentials.
 *
 * @param[in] overwrite Whether to overwrite existing credentials, should be set to true when DTLS connecting is failing.
 * @return 0 on success, negative error code on failure.
 */
static int prv_onboard_device(bool overwrite)
{
	/* If the configured tag contains DTLS credentials, the onboarding process is skipped */
	/* Unless "overwrite" is true,  */
	int err;
	bool exists;
	err = modem_key_mgmt_exists(CONFIG_NCE_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, &exists);

	if (overwrite || !exists)
	{
		struct OSNetwork OSNetwork = {.os_socket = 0};
		os_network_ops_t osNetwork =
			{
				.os_socket = &OSNetwork,
				.nce_os_udp_connect = nce_os_connect,
				.nce_os_udp_send = nce_os_send,
				.nce_os_udp_recv = nce_os_recv,
				.nce_os_udp_disconnect = nce_os_disconnect};
		err = os_auth(&osNetwork, &nceKey);

		if (err)
		{
			LOG_ERR("1NCE SDK onboarding failed, err %d\n", errno);
			return err;
		}

		LOG_INF("Disconnecting from the network to store credentials\n");

		err = lte_lc_offline();

		if (err)
		{
			LOG_ERR("Failed to disconnect from the LTE network, err %d\n", err);
			return err;
		}

		err = store_credentials();

		if (err)
		{
			LOG_ERR("Failed to store credentials, err %d\n", errno);
			return err;
		}

		LOG_INF("Rebooting to ensure changes take effect after saving credentials..");

		sys_arch_reboot(0);
	}
	else
	{
		LOG_INF("Device is already onboarded \n");
	}

	return err;
}

/* Configure DTLS socket */
static int dtls_setup(int fd)
{
	int err;
	int verify;
	int role;

	/* Set up DTLS peer verification */
	enum
	{
		NONE = 0,
		OPTIONAL = 1,
		REQUIRED = 2,
	};

	verify = NONE;

	err = zsock_setsockopt(fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));

	if (err)
	{
		LOG_ERR("[ERR]Failed to setup peer verification, err %d\n", errno);
		return err;
	}

	/* Set up DTLS client mode */
	enum
	{
		CLIENT = 0,
		SERVER = 1,
	};

	role = CLIENT;

	err = zsock_setsockopt(fd, SOL_TLS, TLS_DTLS_ROLE, &role, sizeof(role));

	if (err)
	{
		LOG_ERR("[ERR]Failed to setup DTLS role, err %d\n", errno);
		return err;
	}

	/* Associate the socket with the security tag */
	err = zsock_setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag,
						   sizeof(tls_sec_tag));

	if (err)
	{
		LOG_ERR("[ERR]Failed to setup TLS sec tag, err %d\n", errno);
		return err;
	}

	return 0;
}
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

/** @brief Starts the uplink work item. */
void uplink_work_fn(struct k_work *work)
{
	int err;
	struct sockaddr_storage server = {0};
	struct coap_client coap_client = {0};
	struct coap_client_request req = {
		.method = COAP_METHOD_POST,
		.confirmable = true,
		.fmt = COAP_CONTENT_FORMAT_TEXT_PLAIN,
		.cb = response_cb,
		.path = CONFIG_URI_PATH,
	};

	if (sock < 0)
	{
		LOG_INF("Initializing Uplink socket...");

		err = server_resolve(&server);
		if (err)
		{
			LOG_ERR("Failed to resolve server name");
			return;
		}

#if defined(CONFIG_NCE_ENABLE_DTLS)
		/* Onboard the device */
		err = prv_onboard_device(false);
		if (err)
		{
			LOG_ERR("Deviec onboarding failed, err %d\n", err);
			return;
		}
		LOG_INF("Deviec onboarding successfully \n");
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

#if defined(CONFIG_NCE_ENABLE_DTLS)
		sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_DTLS_1_2);
#else
		sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif
		if (sock < 0)
		{
			LOG_ERR("Failed to create CoAP socket: %d.", -errno);
			return;
		}
#if defined(CONFIG_NCE_ENABLE_DTLS)
		err = dtls_setup(sock);
		if (err)
		{
			LOG_ERR("DTLS setup failed, err %d\n", err);
			return;
		}
#endif
		LOG_INF("Initializing CoAP client");
		err = coap_client_init(&coap_client, NULL);
		if (err)
		{
			LOG_ERR("Failed to initialize CoAP client: %d", err);
			return;
		}
		struct sockaddr_in *addr_in = (struct sockaddr_in *)&server;
		err = zsock_connect(sock, (struct sockaddr *)addr_in, sizeof(struct sockaddr_in));

		if (err)
		{
			LOG_ERR("Failed to Connect to CoAP Server\n");
			return;
		}
		LOG_INF("Successfully connected to CoAP server\n");
	}

#if defined(CONFIG_NCE_ENERGY_SAVER)
	int converted_bytes = 0;
	char message[CONFIG_NCE_PAYLOAD_DATA_SIZE];

	LOG_INF("\nCoAP client POST (Binary Payload)\n");

	Element2byte_gen_t battery_level = {
		.type = E_INTEGER,
		.value.i = 99,
		.template_length = 1};
	Element2byte_gen_t signal_strength = {
		.type = E_INTEGER,
		.value.i = 84,
		.template_length = 1};
	Element2byte_gen_t software_version = {
		.type = E_STRING,
		.value.s = "2.2.1",
		.template_length = 5};

	converted_bytes = os_energy_save(message, 1, 3,
									 battery_level,
									 signal_strength,
									 software_version);

	if (converted_bytes < 0)
	{
		LOG_ERR("os_energy_save error\n");
		return;
	}

	req.payload = message;
	req.len = converted_bytes;
#else
	req.payload = CONFIG_PAYLOAD;
	req.len = strlen(CONFIG_PAYLOAD);
#endif
	wait_for_network();
	/* Send request */
	err = coap_client_req(&coap_client, sock, NULL, &req, NULL);
	if (err)
	{
		LOG_ERR("Failed to send request: %d", err);
		return;
	}

	LOG_INF("CoAP POST request sent sent to %s, resource: %s",
			CONFIG_COAP_SAMPLE_SERVER_HOSTNAME, req.path);

	k_work_schedule(&uplink_work, K_SECONDS(CONFIG_COAP_SAMPLE_REQUEST_INTERVAL_SECONDS));
}
#if defined(CONFIG_NCE_ENABLE_DEVICE_CONTROLLER)
/** @brief Initialize and send a CoAP acknowledgment. */
static int send_coap_ack(int sock, struct coap_packet *packet,
						 struct sockaddr *addr, socklen_t addr_len)
{
	int r;
	struct coap_packet ack;
	uint8_t *data;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data)
	{
		return -ENOMEM;
	}

	r = coap_ack_init(&ack, packet, data, MAX_COAP_MSG_LEN, COAP_RESPONSE_CODE_CHANGED);
	if (r < 0)
	{
		LOG_ERR("Failed to init CoAP ACK \n");
		goto end;
	}
	LOG_HEXDUMP_INF(ack.data, ack.offset, "sent ack:");
	r = zsock_sendto(sock, ack.data, ack.offset, 0, addr, addr_len);
	if (r < 0)
	{
		LOG_ERR("Failed to send CoAP ACK: %d", r);
		goto end;
	}
end:
	k_free(data);
	return r;
}
/** @brief Print CoAP message details. */
void print_coap_options(struct coap_packet *packet)
{
	LOG_INF("CoAP Options:\n");
	// Create a coap_option array to hold the found options
	struct coap_option option_data[16];

	int num_options = coap_find_options(packet, COAP_OPTION_URI_PATH, option_data, 16);
	// If the Uri-Path option exists, extract and print the path
	if (num_options > 0)
	{
		LOG_INF("Complete Path: ");
		for (int i = 0; i < num_options; i++)
		{
			struct coap_option *uri_path_opt = &option_data[i];
			LOG_INF("/%.*s\n", uri_path_opt->len, uri_path_opt->value);
		}
		LOG_INF("\n");
	}

	// Create a coap_option array to hold the found Uri-Query options
	struct coap_option options_query[30];

	// Find the Uri-Query options (option number 15)
	int num_query_options = coap_find_options(packet, COAP_OPTION_URI_QUERY, options_query, 30);
	// If the Uri-Query options exist, extract and print the query
	if (num_query_options > 0)
	{
		LOG_INF("Query: ");
		for (int i = 0; i < num_query_options; i++)
		{
			struct coap_option *uri_query_opt = &options_query[i];
			LOG_INF("%.*s", uri_query_opt->len, uri_query_opt->value);
			if (i < num_query_options - 1)
			{
				LOG_INF("&");
			}
		}
		LOG_INF("\n");
	}
}
bool check_and_print_coap_response_code(struct coap_packet *packet)
{
	uint8_t code_class = coap_header_get_code(packet) / COAP_CODE_CLASS_SIZE;
	uint8_t code_detail = coap_header_get_code(packet) % COAP_CODE_CLASS_SIZE;

	/* Check if the response code is in the success range (2.xx) */
	bool success = (code_class == COAP_SUCCESS_CODE_CLASS);

	LOG_INF("Response Code: %d.%02d, Success: %s\n",
			code_class, code_detail, success ? "true" : "false");

	return success;
}
void print_coap_payload(struct coap_packet *packet)
{
	uint16_t payload_len;
	const uint8_t *payload = coap_packet_get_payload(packet, &payload_len);

	LOG_DBG("CoAP Payload len: %d\n", payload_len);

	if (payload_len > 0)
	{
		LOG_INF("CoAP Payload: %s\n", payload);
	}
}
void print_coap_header(struct coap_packet *packet)
{
	LOG_INF("CoAP Header:\n");
	LOG_INF("Version: %d\n", coap_header_get_version(packet));
	LOG_INF("Type: %s\n", coap_header_get_type(packet) == COAP_TYPE_CON ? "CON" : "NON");
	check_and_print_coap_response_code(packet);
	LOG_INF("Message ID: %u\n", coap_header_get_id(packet));
}
void print_coap_message(struct coap_packet *packet)
{
	print_coap_header(packet);
	print_coap_options(packet);
	print_coap_payload(packet);
}
/** @brief Downlink function: Listens for incoming CoAP messages */
void downlink_work_fn(struct k_work *work)
{
	int r;
	char buffer[CONFIG_NCE_RECEIVE_BUFFER_SIZE];
	struct coap_packet response;
	struct sockaddr_in my_addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_ANY),
		.sin_port = htons(CONFIG_NCE_RECV_PORT)};
	struct sockaddr sender_addr;
	socklen_t sender_addr_len = sizeof(sender_addr);
	k_mutex_lock(&socket_mutex, K_FOREVER);
	// Create and bind UDP socket only once
	if (recv_fd < 0)
	{
		recv_fd = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (recv_fd < 0)
		{
			LOG_ERR("Error while creating socket: %d\n", errno);
			k_mutex_unlock(&socket_mutex);
			return;
		}
		LOG_INF("Receiver socket created successfully\n");

		// Bind to the set port and IP
		if (zsock_bind(recv_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_in)) < 0)
		{
			LOG_ERR("Couldn't bind to the port: %d\n", errno);
			k_mutex_unlock(&socket_mutex);
			return;
		}
		LOG_INF("Listening for incoming messages...\n");

		// Set the timeout value
		struct timeval timeout;
		timeout.tv_sec = 60;
		timeout.tv_usec = 0;

		// Set the socket option for timeout
		if (setsockopt(recv_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
		{
			LOG_ERR("Failed to set the socket option for timeout");
			k_mutex_unlock(&socket_mutex);
			return;
		}
	}
	k_mutex_unlock(&socket_mutex);
	ssize_t received_bytes;

	LOG_INF("Listening for incoming messages...\n");
	received_bytes = recvfrom(recv_fd, buffer, sizeof(buffer) - 1, 0,
							  &sender_addr, &sender_addr_len);
	if (received_bytes < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			LOG_INF("Socket timeout: No message received\n");
		}
		else
		{
			LOG_ERR("recvfrom() error: %d\n", errno);
		}
	}
	buffer[received_bytes] = '\0';

	/* Parse CoAP message */
	r = coap_packet_parse(&response, buffer, received_bytes, NULL, 0);
	if (r < 0)
	{
		LOG_ERR("coap_packet_parse() failed: %d\n", r);
		LOG_HEXDUMP_INF(buffer, received_bytes, "Received raw data:");
	}

	LOG_HEXDUMP_INF(buffer, received_bytes, "Received raw data:");
	print_coap_message(&response);

	// Reply with CoAP ACK
	r = send_coap_ack(recv_fd, &response, &sender_addr, sender_addr_len);
	if (r < 0)
	{
		LOG_ERR("send_coap_ack() failed: %d\n", r);
	}
	else
	{
		LOG_INF("CoAP ACK SUCCESS\n");
	}
	k_work_schedule(&downlink_work, K_SECONDS(CONFIG_NCE_CONTROLLER_INTERVAL));
}
#endif
void init_work()
{
	LOG_INF("Initializing work items");
	k_work_init_delayable(&uplink_work, uplink_work_fn);
#if defined(CONFIG_NCE_ENABLE_DEVICE_CONTROLLER)
	k_work_init_delayable(&downlink_work, downlink_work_fn);
#endif
}
static void l4_event_handler(struct net_mgmt_event_callback *cb,
							 uint32_t event,
							 struct net_if *iface)
{
	switch (event)
	{
	case NET_EVENT_L4_CONNECTED:
		LOG_INF("Network connectivity established");
		k_mutex_lock(&network_connected_lock, K_FOREVER);
		is_connected = true;
		k_condvar_signal(&network_connected);
		k_mutex_unlock(&network_connected_lock);
		break;
	case NET_EVENT_L4_DISCONNECTED:
		LOG_INF("Network connectivity lost");
		k_mutex_lock(&network_connected_lock, K_FOREVER);
		is_connected = false;
		k_mutex_unlock(&network_connected_lock);
		break;
	default:
		/* Don't care */
		return;
	}
}
static void connectivity_event_handler(struct net_mgmt_event_callback *cb,
									   uint32_t event,
									   struct net_if *iface)
{
	if (event == NET_EVENT_CONN_IF_FATAL_ERROR)
	{
		LOG_ERR("NET_EVENT_CONN_IF_FATAL_ERROR");
		FATAL_ERROR();
		return;
	}
}
int main(void)
{
	int err;

	LOG_INF("The 1NCE CoAP client sample started");

	/* Setup handler for Zephyr NET Connection Manager events and Connectivity layer. */
	net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
	net_mgmt_add_event_callback(&l4_cb);

	net_mgmt_init_event_callback(&conn_cb, connectivity_event_handler, CONN_LAYER_EVENT_MASK);
	net_mgmt_add_event_callback(&conn_cb);

	/* Bring all network interfaces up.
	 * Wi-Fi or LTE depending on the board that the sample was built for.
	 */
	LOG_INF("Bringing network interface up and connecting to the network");

	err = conn_mgr_all_if_up(true);
	if (err)
	{
		LOG_ERR("conn_mgr_all_if_up, error: %d", err);
		FATAL_ERROR();
		return err;
	}

	err = conn_mgr_all_if_connect(true);
	if (err)
	{
		LOG_ERR("conn_mgr_all_if_connect, error: %d", err);
		FATAL_ERROR();
		return err;
	}

	/* Resend connection status if the sample is built for NATIVE_SIM.
	 * This is necessary because the network interface is automatically brought up
	 * at SYS_INIT() before main() is called.
	 * This means that NET_EVENT_L4_CONNECTED fires before the
	 * appropriate handler l4_event_handler() is registered.
	 */
	if (IS_ENABLED(CONFIG_BOARD_NATIVE_SIM))
	{
		conn_mgr_mon_resend_status();
	}
	/* Initialize work items */
	init_work();
	k_work_schedule(&uplink_work, K_SECONDS(CONFIG_COAP_SAMPLE_REQUEST_INTERVAL_SECONDS));
#if defined(CONFIG_NCE_ENABLE_DEVICE_CONTROLLER)
	k_work_schedule(&downlink_work, K_SECONDS(CONFIG_NCE_CONTROLLER_INTERVAL));
#endif
	k_sleep(K_SECONDS(1));

	return 0;
}
