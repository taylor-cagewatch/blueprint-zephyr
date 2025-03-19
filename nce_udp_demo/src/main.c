/******************************************************************************
 * @file       main.c
 * @brief      UDP Demo
 * @details    This file contains the main function, initialization, and implementation for
 *             communicating with 1NCE endpoints via UDP Protocol. It supports sending compressed
 *             payloads using the Energy Saver feature.
 * @author     Hatim Jamali & Mohamed Abdelmaksoud
 * @date       2025-02
 ******************************************************************************/

/******************************************************************************
 * Includes
 ******************************************************************************/

#include <zephyr/kernel.h>
#include <stdio.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/net/socket.h>
#include <nce_iot_c_sdk.h>

#if defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
#include <zephyr/drivers/gpio.h>
/*
 * Thingy:91 LEDs
 */
static struct gpio_dt_spec ledRed = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
														{0});
static struct gpio_dt_spec ledGreen = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios,
														  {0});
static struct gpio_dt_spec ledBlue = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios,
														 {0});
#endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

/******************************************************************************
 * Macros and Constants
 ******************************************************************************/

/* LOG Macros */
LOG_MODULE_REGISTER(UDP_CLIENT, CONFIG_LOG_DEFAULT_LEVEL);
#define UDP_IP_HEADER_SIZE 28
#define STACK_SIZE 512
#define STACK_SIZE2 2048
#define THREAD_PRIORITY 5

/******************************************************************************
 * Static Variables
 ******************************************************************************/

/** @brief Kernel stack and threading configurations */
K_THREAD_STACK_DEFINE(thread_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_stack2, STACK_SIZE2);
static K_SEM_DEFINE(lte_connected_sem, 0, 1);
static struct k_work_delayable server_transmission_work;
static struct k_work_delayable control_recv_work;
struct k_thread thread_recv, thread_send;
static int client_fd, recv_fd;
/******************************************************************************
 * Functions
 ******************************************************************************/
#if defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
/**
 * @brief Configures the LED GPIOs if the device is ready.
 */
void configureLeds()
{
	int ret = 0;
	if (ledRed.port && !device_is_ready(ledRed.port))
	{
		printk("[ERR] Error %d: LED device %s is not ready; ignoring it\n",
			   ret, ledRed.port->name);
		ledRed.port = NULL;
	}
	if (ledRed.port)
	{
		ret = gpio_pin_configure_dt(&ledRed, GPIO_OUTPUT);
		if (ret != 0)
		{
			printk("[ERR] Error %d: failed to configure LED device %s pin %d\n",
				   ret, ledRed.port->name, ledRed.pin);
			ledRed.port = NULL;
		}
	}

	if (ledGreen.port && !device_is_ready(ledGreen.port))
	{
		printk("[ERR] Error %d: LED device %s is not ready; ignoring it\n",
			   ret, ledGreen.port->name);
		ledGreen.port = NULL;
	}
	if (ledGreen.port)
	{
		ret = gpio_pin_configure_dt(&ledGreen, GPIO_OUTPUT);
		if (ret != 0)
		{
			printk("[ERR] Error %d: failed to configure LED device %s pin %d\n",
				   ret, ledGreen.port->name, ledGreen.pin);
			ledGreen.port = NULL;
		}
	}

	if (ledBlue.port && !device_is_ready(ledBlue.port))
	{
		printk("[ERR] Error %d: LED device %s is not ready; ignoring it\n",
			   ret, ledBlue.port->name);
		ledBlue.port = NULL;
	}
	if (ledBlue.port)
	{
		ret = gpio_pin_configure_dt(&ledBlue, GPIO_OUTPUT);
		if (ret != 0)
		{
			printk("[ERR] Error %d: failed to configure LED device %s pin %d\n",
				   ret, ledBlue.port->name, ledBlue.pin);
			ledBlue.port = NULL;
		}
	}
}
#endif
/**
 * @brief Handles LTE network events.
 *
 * @param evt Pointer to the LTE event structure.
 */
static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type)
	{
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
			(evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING))
		{
			break;
		}

		printk("[INF] Network registration status: %s\n",
			   evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ? "Connected - home" : "Connected - roaming");
		k_sem_give(&lte_connected_sem);
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		printk("[INF] PSM parameter update: TAU: %d s, Active time: %d s\n",
			   evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE:
		printk("[INF] eDRX parameter update: eDRX: %.2f s, PTW: %.2f s\n",
			   (double)evt->edrx_cfg.edrx, (double)evt->edrx_cfg.ptw);
		break;
	case LTE_LC_EVT_RRC_UPDATE:
		printk("[INF] RRC mode: %s\n",
			   evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle\n");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		printk("[INF] LTE cell changed: Cell ID: %d, Tracking area: %d\n",
			   evt->cell.id, evt->cell.tac);
		break;
	case LTE_LC_EVT_RAI_UPDATE:
		/* RAI notification is supported by modem firmware releases >= 2.0.2 */
		printk("[INF] RAI configuration update: "
			   "Cell ID: %d, MCC: %d, MNC: %d, AS-RAI: %d, CP-RAI: %d\n",
			   evt->rai_cfg.cell_id,
			   evt->rai_cfg.mcc,
			   evt->rai_cfg.mnc,
			   evt->rai_cfg.as_rai,
			   evt->rai_cfg.cp_rai);
		break;
	default:
		break;
	}
}

/**
 * @brief Sends a UDP packet at scheduled intervals.
 */
static void server_transmission_work_fn(struct k_work *work)
{
	int err;
#if !defined(CONFIG_NCE_ENERGY_SAVER)
	char buffer[] = CONFIG_PAYLOAD;
#else
	char buffer[CONFIG_PAYLOAD_DATA_SIZE];

	Element2byte_gen_t battery_level = {.type = E_INTEGER, .value.i = 99, .template_length = 1};
	Element2byte_gen_t signal_strength = {.type = E_INTEGER, .value.i = 84, .template_length = 1};
	Element2byte_gen_t software_version = {.type = E_STRING, .value.s = "2.2.1", .template_length = 5};
	err = os_energy_save(buffer, 1, 3, battery_level, signal_strength, software_version);
	if (err < 0)
	{
		printk("[ERR] Failed to save energy, %d\n", errno);
	}
	printk("[INF] Transmitting UDP/IP payload of %d bytes to the ",
		   CONFIG_PAYLOAD_DATA_SIZE + UDP_IP_HEADER_SIZE);
#endif

	printk("[INF] Hostname %s, port number %d\n",
		   CONFIG_UDP_SERVER_HOSTNAME,
		   CONFIG_UDP_SERVER_PORT);

	err = send(client_fd, buffer, sizeof(buffer), 0);
	if (err < 0)
	{
		printk("[ERR] Failed to transmit UDP packet, %d\n", errno);
	}

#if defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
	if (ledBlue.port)
	{
		gpio_pin_set_dt(&ledBlue, 0);
	}
	if (ledGreen.port)
	{
		gpio_pin_set_dt(&ledGreen, 100);
	}
#endif

	k_work_schedule(&server_transmission_work,
					K_SECONDS(CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS));
}

/**
 * @brief Receives UDP packets and processes them.
 */
static void reciever_work_fn(struct k_work *work)
{
	static struct addrinfo *control_recv;
	static struct addrinfo hints_control_recv =
		{
			.ai_family = AF_INET,
			.ai_socktype = SOCK_DGRAM,
		};
	int hints_control_recv_struct_length = sizeof(hints_control_recv);
	char buffer[256];
	struct sockaddr_in my_addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_ANY),
		.sin_port = htons(CONFIG_NCE_RECV_PORT)};

	// Create socket
	recv_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (recv_fd < 0)
	{
		printk("[ERR] Error while creating socket\n");
		goto cleanup;
	}
	printf("[INF] Socket created successfully\n");

	// Bind to the set port and IP:
	if (zsock_bind(recv_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_in)) < 0)
	{
		printk("[ERR] Couldn't bind to the port\n");
		goto cleanup;
	}
	printk("[INF] Listening for incoming messages...\n\n");

	// Set the timeout value
	struct timeval timeout;
	timeout.tv_sec = 60;
	timeout.tv_usec = 0;
	if (setsockopt(recv_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
	{
		perror("[ERR] failed to set the socket option for timeout");
		goto cleanup;
	}

	ssize_t received_bytes = recvfrom(recv_fd, buffer, sizeof(buffer) - 1, 0,
									  (struct sockaddr *)control_recv, &hints_control_recv_struct_length);
	if (received_bytes < 0)
	{
		printf("[WARN] No message received\n");
		goto cleanup;
	}

	buffer[received_bytes] = '\0';
	printk("[INF] Received message: %s\n", buffer);
	k_work_schedule(&control_recv_work,
					K_SECONDS(CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS));

cleanup:
	close(recv_fd);
	return;
}
/**
 * @brief Disconnects the socket connection.
 */
static void socket_disconnect(void)
{
	(void)close(client_fd);
}
/**
 * @brief Establishes a UDP socket connection.
 */
static int socket_connect(void)
{
	int err;
	static struct addrinfo *res;
	static struct addrinfo hints =
		{
			.ai_family = AF_INET,
			.ai_socktype = SOCK_DGRAM,
		};
	err = zsock_getaddrinfo(CONFIG_UDP_SERVER_HOSTNAME, NULL, &hints, &res);
	if (err < 0)
	{
		printk("[ERR] getaddrinfo() failed, err %d\n", errno);
		err = -errno;
		goto error;
	}
	((struct sockaddr_in *)res->ai_addr)->sin_port = htons(CONFIG_UDP_SERVER_PORT);

	client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (client_fd < 0)
	{
		printk("[ERR] Failed to create UDP socket: %d\n", errno);
		err = -errno;
		goto error;
	}

	err = connect(client_fd, (struct sockaddr *)res->ai_addr,
				  sizeof(struct sockaddr_in));
	if (err < 0)
	{
		printk("[ERR] Connect failed : %d\n", errno);
		goto error;
	}

	return 0;

error:
	socket_disconnect();

	return err;
}
/**
 * @brief Initializes the work queue.
 */
static void work_init(void)
{
	k_work_init_delayable(&server_transmission_work,
						  server_transmission_work_fn);
	k_work_init_delayable(&control_recv_work,
						  reciever_work_fn);
}
/**
 * @brief Background thread function handling incoming messages.
 */
void background_thread_func()
{
	while (1)
	{
		// If a message is received or event occurs, schedule the work
		k_work_schedule(&control_recv_work, K_NO_WAIT);
		k_sleep(K_SECONDS(2));
	}
}
/**
 * @brief Front thread function handling outgoing UDP packets.
 */
void front_thread_func()
{
	while (1)
	{
		// Sending the UDP Packet
		k_work_schedule(&server_transmission_work, K_NO_WAIT);
		k_sleep(K_SECONDS(1));
	}
}

/******************************************************************************
 * Main Function
 ******************************************************************************/
int main(void)
{
	int err;

#if defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
	configureLeds();
	k_sleep(K_SECONDS(10));
	if (ledRed.port)
	{
		gpio_pin_set_dt(&ledRed, 100);
	}
#endif

	printk("[INF] 1NCE UDP sample has started\n");

	work_init();

	err = nrf_modem_lib_init();
	if (err)
	{
		printk("[ERR] Failed to initialize modem library, error: %d\n", err);
		return -1;
	}
	err = lte_lc_connect_async(lte_handler);
	if (err)
	{
		printk("[ERR] Failed to connect to LTE network, error: %d\n", err);
		return -1;
	}

	k_sem_take(&lte_connected_sem, K_FOREVER);
#if defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
	if (ledRed.port)
	{
		gpio_pin_set_dt(&ledRed, 0);
	}
	if (ledBlue.port)
	{
		gpio_pin_set_dt(&ledBlue, 100);
	}
#endif

	err = socket_connect();
	if (err)
	{
		printk("[ERR] Not able to connect to UDP server\n");
		return -1;
	}
	k_tid_t thread_recv_id = k_thread_create(&thread_recv, thread_stack,
											 K_THREAD_STACK_SIZEOF(thread_stack),
											 background_thread_func,
											 NULL, NULL, NULL,
											 THREAD_PRIORITY, 0, K_NO_WAIT);
	k_tid_t thread_send_id = k_thread_create(&thread_send, thread_stack2,
											 K_THREAD_STACK_SIZEOF(thread_stack2),
											 front_thread_func,
											 NULL, NULL, NULL,
											 THREAD_PRIORITY, 0, K_NO_WAIT);
	// Start the thread
	k_thread_start(thread_send_id);
	k_thread_start(thread_recv_id);

	// Delay or wait for the threads to complete
	k_sleep(K_SECONDS(2));
	return 0;
}
