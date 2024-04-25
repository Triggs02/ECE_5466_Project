/*
 * app_zmod_bindings.c
 *
 *  Created on: Mar 11, 2024
 *      Author: rjp5t
 */

#include <pthread.h>
#include <assert.h>
#include <string.h>
#include "FreeRTOS.h"
#include <task.h>
#include "ti_drivers_config.h"
#include "zmod4xxx_types.h"
#include "zmod4410_config_iaq2.h"
#include "zmod4xxx.h"
#include "zmod4xxx_cleaning.h"
#include "iaq_2nd_gen.h"
#include <ti/drivers/GPIO.h>
#include <ti/bleapp/menu_module/menu_module.h>
#include <app_main.h>

static pthread_t zmodThread;
static I2C_Handle zmodHandle;
static uint8_t dataBufTmp[257];

void app_zmod4xxx_hal_delay_ms(uint32_t ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

int8_t app_zmod4xxx_i2c_read(uint8_t addr, uint8_t reg_addr, uint8_t *data_buf, uint8_t len) {
    I2C_Transaction transaction = {0};
    transaction.targetAddress = addr;

    // Read from I2C target device
    transaction.writeBuf = &reg_addr;
    transaction.writeCount = 1;
    transaction.readBuf = data_buf;
    transaction.readCount = len;
    if (!I2C_transfer(zmodHandle, &transaction)) {
        return ERROR_I2C;
    }

    return ZMOD4XXX_OK;
}

int8_t app_zmod4xxx_i2c_write(uint8_t addr, uint8_t reg_addr, uint8_t *data_buf, uint8_t len) {
    I2C_Transaction transaction = {0};
    transaction.targetAddress = addr;

    dataBufTmp[0] = reg_addr;
    memcpy(&dataBufTmp[1], data_buf, len);

    // Read from I2C target device
    transaction.writeBuf = &dataBufTmp;
    transaction.writeCount = len + 1;
    transaction.readBuf = data_buf;
    transaction.readCount = len;
    if (!I2C_transfer(zmodHandle, &transaction)) {
        return ERROR_I2C;
    }

    return ZMOD4XXX_OK;
}

int8_t app_zmod4xxx_hal_init(zmod4xxx_dev_t *dev) {
    I2C_init();

    // initialize optional I2C bus parameters
    I2C_Params params;
    I2C_Params_init(&params);
    params.bitRate = CONFIG_I2C_0_MAXBITRATE;

    // Open I2C bus for usage
    zmodHandle = I2C_open(CONFIG_I2C_0, &params);
    if (zmodHandle == NULL) {
        return 1;
    }

    dev->write = &app_zmod4xxx_i2c_write;
    dev->read = &app_zmod4xxx_i2c_read;
    dev->delay_ms = &app_zmod4xxx_hal_delay_ms;
    return 0;
}


void *app_zmod4xxx_run(void* arg) {
    int8_t ret;
    zmod4xxx_dev_t dev;


    /* Sensor specific variables */
    uint8_t zmod4xxx_status;
    uint8_t track_number[ZMOD4XXX_LEN_TRACKING];
    uint8_t adc_result[ZMOD4410_ADC_DATA_LEN];
    uint8_t prod_data[ZMOD4410_PROD_DATA_LEN];
    iaq_2nd_gen_handle_t algo_handle;
    iaq_2nd_gen_results_t algo_results;
    iaq_2nd_gen_inputs_t algo_input;

    MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Initializing ZMOD4410");

    /**** TARGET SPECIFIC FUNCTION ****/
    /*
     * To allow the example running on customer-specific hardware, the init_hardware
     * function must be adapted accordingly. The mandatory funtion pointers *read,
     * *write and *delay require to be passed to "dev" (reference files located
     * in "dependencies/zmod4xxx_api/HAL" directory). For more information, read
     * the Datasheet, section "I2C Interface and Data Transmission Protocol".
     */
    ret = app_zmod4xxx_hal_init(&dev);
    if (ret) {
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error %d during initialize zmod hardware",
                          ret);
        goto exit;
    }
    /**** TARGET SPECIFIC FUNCTION ****/

    /* Sensor related data */
    dev.i2c_addr = ZMOD4410_I2C_ADDR;
    dev.pid = ZMOD4410_PID;
    dev.init_conf = &zmod_iaq2_sensor_cfg[INIT];
    dev.meas_conf = &zmod_iaq2_sensor_cfg[MEASUREMENT];
    dev.prod_data = prod_data;

    /* Read product ID and configuration parameters. */
    ret = zmod4xxx_read_sensor_info(&dev);
    if (ret) {
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error %d during reading sensor information",
                          ret);
        goto exit;
    }
    /*
     * Retrieve sensors unique tracking number and individual trimming information.
     * Provide this information when requesting support from Renesas.
     */
    ret = zmod4xxx_read_tracking_number(&dev, track_number);
    if (ret) {
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error %d during reading tracking number",
                          ret);
        goto exit;
    }
    static_assert(sizeof(track_number) == 6, "Tracking number not expected size");
    MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE1, 0, "Sensor tracking number: x0000%02x%02x%02x%02x%02x%02x",
                      track_number[0], track_number[1], track_number[2],
                      track_number[3], track_number[4], track_number[5]);
    static_assert(sizeof(prod_data) == 7, "Prod data not expected size");
    MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE2, 0, "Sensor trimming data: %i %i %i %i %i %i %i",
                          prod_data[0], prod_data[1], prod_data[2], prod_data[3],
                          prod_data[4], prod_data[5], prod_data[6]);

    /*
     * Start the cleaning procedure. Check the Programming Manual on indications
     * of usage. IMPORTANT NOTE: The cleaning procedure can be run only once
     * during the modules lifetime and takes 1 minute (blocking).
     */
//    MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Starting cleaning procedure. This might take up to 1 min ...");
//    ret = zmod4xxx_cleaning_run(&dev);
//    if (ERROR_CLEANING == ret) {
//        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Skipping cleaning procedure. It has already been performed!");
//    } else if (ret) {
//        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error %d during cleaning procedure", ret);
//        goto exit;
//    }

    /* Determine calibration parameters and configure measurement. */
    ret = zmod4xxx_prepare_sensor(&dev);
    if (ret) {
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error %d during preparation of the sensor", ret);
        goto exit;
    }

    /*
     * One-time initialization of the algorithm. Handle passed to calculation
     * function.
     */
    ret = init_iaq_2nd_gen(&algo_handle);
    if (ret) {
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error %d during initializing algorithm", ret);
        goto exit;
    }

    while ( 1 ) {
        /* Start a measurement. */
        ret = zmod4xxx_start_measurement(&dev);
        if (ret) {
            MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error %d during starting measurement", ret);
            goto reading_fail;
        }
        /*
         * Perform delay. Required to keep proper measurement timing and keep algorithm accuracy.
         * For more information, read the Programming Manual, section
         * "Interrupt Usage and Measurement Timing".
         */
        dev.delay_ms(ZMOD4410_IAQ2_SAMPLE_TIME);

        /* Verify completion of measurement sequence. */
        ret = zmod4xxx_read_status(&dev, &zmod4xxx_status);
        if (ret) {
            MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error %d during reading sensor status", ret);
            goto reading_fail;
        }
        /* Check if measurement is running. */
        if (zmod4xxx_status & STATUS_SEQUENCER_RUNNING_MASK) {
            /*
             * Check if reset during measurement occured. For more information,
             * read the Programming Manual, section "Error Codes".
             */
            ret = zmod4xxx_check_error_event(&dev);
            switch (ret) {
            case ERROR_POR_EVENT:
                MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Measurement completion fault. Unexpected sensor reset.");
                break;
            case ZMOD4XXX_OK:
                MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Measurement completion fault. Wrong sensor setup.");
                break;
            default:
                MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error during reading status register (%d)", ret);
                break;
            }
            dev.delay_ms(5000);
            continue;
        }
        /* Read sensor ADC output. */
        ret = zmod4xxx_read_adc_result(&dev, adc_result);
        if (ret) {
            MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error %d during reading of ADC results, exiting program!",
                              ret);
            goto reading_fail;
        }

        /*
         * Check validity of the ADC results. For more information, read the
         * Programming Manual, section "Error Codes".
         */
        ret = zmod4xxx_check_error_event(&dev);
        if (ret) {
            MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error during reading status register (%d)", ret);
            goto reading_fail;
        }

        /*
         * Assign algorithm inputs: raw sensor data and ambient conditions.
         * Production code should use measured temperature and humidity values.
         */
        algo_input.adc_result = adc_result;
        algo_input.humidity_pct = 50.0;
        algo_input.temperature_degc = 20.0;

        /* Calculate algorithm results. */
        ret = calc_iaq_2nd_gen(&algo_handle, &dev, &algo_input, &algo_results);

//        printf("*********** Measurements ***********\n");
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE1, 0, " EtOH = %6.3f ppm", algo_results.etoh);
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE2, 0, " TVOC = %6.3f mg/m^3", algo_results.tvoc);
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE3, 0, " eCO2 = %4.0f ppm", algo_results.eco2);
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE4, 0, " IAQ  = %4.1f", algo_results.iaq);
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE4 + 1, 0, " Rcda = %.3f kOhm", pow(10, algo_results.log_rcda) / 1e3);
        for (int i = 0; i < 13; i++) {
            MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE4 + 2 + i, 0, " Rmox[%d] = %.3f kOhm", i, algo_results.rmox[i] / 1e3);
        }

        /* Check validity of the algorithm results. */
        switch (ret) {
        case IAQ_2ND_GEN_STABILIZATION:
            /* The sensor should run for at least 100 cycles to stabilize.
             * Algorithm results obtained during this period SHOULD NOT be
             * considered as valid outputs! */
            MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Readings from Warm-Up!");
            break;
        case IAQ_2ND_GEN_OK:
            MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Readings Valid!");
            break;
        /*
        * Notification from Sensor self-check. For more information, read the
        * Programming Manual, section "Troubleshoot Sensor Damage (Sensor Self-Check)".
        */
        case IAQ_2ND_GEN_DAMAGE:
            MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Error: Sensor probably damaged. Algorithm results may be incorrect.");
            break;
        /* Exit program due to unexpected error. */
        default:
            MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0, "Unexpected Error during algorithm calculation (%d)", ret);
            goto reading_fail;
        }

        SendUpdateValue(algo_results.tvoc);
        // SendUpdateValue(algo_results.rmox[12] / 3000000.0);
//        SendUpdateValue((1E6 / algo_results.rmox[12]) + 0.2);
        continue;

reading_fail:
        for (int i = 0; i < 10; i++) {
            GPIO_write(CONFIG_GPIO_LED_RED, CONFIG_GPIO_LED_ON);
            dev.delay_ms(250);
            GPIO_write(CONFIG_GPIO_LED_RED, CONFIG_GPIO_LED_OFF);
            dev.delay_ms(250);
        }
    };

exit:
    GPIO_write(CONFIG_GPIO_LED_RED, CONFIG_GPIO_LED_ON);

    return NULL;
}

void app_zmod4xxx_init(void) {
    pthread_create(&zmodThread, NULL, app_zmod4xxx_run, NULL);
}
