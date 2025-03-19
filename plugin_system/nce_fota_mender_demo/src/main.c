/**
 *
 * @author  Hatim Jamali & Mohamed Abdelmaksoud
 * @date 10 Jan 2024
 * 
 */

#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include <dfu/dfu_target_mcuboot.h>
#include <modem/nrf_modem_lib.h>
#include <custom_fota_download.h>
#include <stdio.h>
#include "update.h"
#include "nce_mender_client.h"
#include "led_control.h"

#define INIT_ATTEMPTS 3

static void fota_dl_handler(const struct fota_download_evt *evt)
{
	switch (evt->id) {
	case CUSTOM_FOTA_DOWNLOAD_EVT_ERROR:
		printk("[INF] Received error from fota_download\n");
		if(evt->cause == CUSTOM_FOTA_DOWNLOAD_ERROR_CAUSE_INVALID_UPDATE ){
			printk("[ERR] Invalid Update\n");
			fota_stop(0);
		}
		else
		{
			printk("[ERR] Download Error\n");
			fota_stop(1);
		}
		
		break;

	case CUSTOM_FOTA_DOWNLOAD_EVT_FINISHED:

		printk("[iNF] Firmware Downloaded. Rebooting in 15 seconds to apply new firmware\n");
		fota_done();
		break;

	default:
		break;
	}
}

void main(void)
{
	int err,i=0;

	printk("[INF] 1NCE FOTA Mender demo started\n");
	long_led_pattern(LED_CONNECTING);
	err = nrf_modem_lib_init();
	if (err) {
		printk("[ERR] Failed to initialize modem library!");
		return;
	}
	printk("boot_write_img_confirmed() \n");
	boot_write_img_confirmed();
	printk("[INF] custom_fota_download_init() \n");
	err = custom_fota_download_init(fota_dl_handler);
	if (err != 0) {
		printk("[ERR] custom_fota_download_init() failed, err %d\n", err);
		return;
	}

	for ( i = 0; i <= INIT_ATTEMPTS; i++)
	{
	err = fota_init(&(struct fota_init_params){
					.update_start = fota_start,
				});
	if (err != 0) {
		printk("[ERR] fota_init() failed, err %d\n", err);
		long_led_pattern(LED_FAILURE);
		return;
	} else {
		break;	
	} 

	}
	

}
