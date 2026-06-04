#include "adc.h"

LOG_MODULE_REGISTER(adc, CONFIG_LOG_DEFAULT_LEVEL);

static ads1220_dev_t ads1220_device = {
    .drdy_pin = ADS1220_DRDY_PIN
    // SPI configuration is handled by device tree and ads1220_init()
};

int adc_init(void) {
    int ret = ads1220_init(&ads1220_device);
    if (ret < 0) {
        LOG_ERR("Failed to initialize ADS1220: %d", ret);
        return ret;
    }

    // Configure ADS1220 for typical usage
    ads1220_set_gain(&ads1220_device, ADS1220_GAIN_1);
    ads1220_set_data_rate(&ads1220_device, ADS1220_DR_LVL_0); // 20 SPS
    ads1220_set_operating_mode(&ads1220_device, ADS1220_NORMAL_MODE);
    ads1220_set_conversion_mode(&ads1220_device, ADS1220_SINGLE_SHOT);
    ads1220_set_vref_source(&ads1220_device, ADS1220_VREF_INT); // Internal 2.048V reference
    ads1220_set_fir_filter(&ads1220_device, ADS1220_50HZ_60HZ); // 50Hz/60Hz rejection

    LOG_INF("ADS1220_WE ADC initialized successfully");
    return 0;
}

int ads1220_read_differential(ads1220_mux_t mux_config, int32_t *value) {
    if (!value) {
        return -EINVAL;
    }

    // Set the mux configuration
    ads1220_set_compare_channels(&ads1220_device, mux_config);

    // Get the data
    *value = ads1220_get_raw_data(&ads1220_device);

    LOG_DBG("Differential read mux=%d: raw=%d", mux_config, *value);
    return 0;
}

int ads1220_read_single_ended(uint8_t channel, int32_t *value) {
    if (!value || channel > 3) {
        return -EINVAL;
    }

    ads1220_mux_t mux_config;
    switch (channel) {
        case 0: mux_config = ADS1220_MUX_0_AVSS; break;
        case 1: mux_config = ADS1220_MUX_1_AVSS; break;
        case 2: mux_config = ADS1220_MUX_2_AVSS; break;
        case 3: mux_config = ADS1220_MUX_3_AVSS; break;
        default: return -EINVAL;
    }

    return ads1220_read_differential(mux_config, value);
}

int adc_read_channel(uint8_t channel, int32_t *value) {
    return ads1220_read_single_ended(channel, value);
}

float adc_raw_to_voltage(int32_t raw_value) {
    return ads1220_get_voltage_mv(&ads1220_device) / 1000.0f; // Convert mV to V
}

void adc_test_loop(void) {
    LOG_INF("Starting ADS1220_WE ADC test loop");

    while (1) {
        // Test differential channels
        LOG_INF("=== Differential Readings ===");

        int32_t diff_value;
        int ret;

        // AIN0-AIN1 differential
        ret = ads1220_read_differential(ADS1220_MUX_0_1, &diff_value);
        if (ret == 0) {
            float voltage = ads1220_get_voltage_mv(&ads1220_device) / 1000.0f;
            LOG_INF("AIN0-AIN1: %d (%.6fV)", diff_value, (double)voltage);
        }

        // AIN2-AIN3 differential
        ret = ads1220_read_differential(ADS1220_MUX_2_3, &diff_value);
        if (ret == 0) {
            float voltage = ads1220_get_voltage_mv(&ads1220_device) / 1000.0f;
            LOG_INF("AIN2-AIN3: %d (%.6fV)", diff_value, (double)voltage);
        }

        // Test single-ended channels
        LOG_INF("=== Single-Ended Readings ===");
        for (uint8_t ch = 0; ch < 4; ch++) {
            int32_t adc_value;
            ret = adc_read_channel(ch, &adc_value);

            if (ret == 0) {
                // Read again to get voltage through the library
                ads1220_set_compare_channels(&ads1220_device,
                    (ch == 0) ? ADS1220_MUX_0_AVSS :
                    (ch == 1) ? ADS1220_MUX_1_AVSS :
                    (ch == 2) ? ADS1220_MUX_2_AVSS :
                                ADS1220_MUX_3_AVSS);

                float voltage = ads1220_get_voltage_mv(&ads1220_device) / 1000.0f;
                LOG_INF("AIN%d: %d (%.6fV)", ch, adc_value, (double)voltage);
            } else {
                LOG_ERR("Failed to read channel %d: %d", ch, ret);
            }

            k_msleep(200);
        }

        // Test temperature sensor
        LOG_INF("=== Temperature Reading ===");
        float temperature = ads1220_get_temperature(&ads1220_device);
        LOG_INF("Temperature: %.2f°C", (double)temperature);

        // Test different gains
        LOG_INF("=== Gain Test (AIN0-AIN1) ===");
        ads1220_gain_t gains[] = {ADS1220_GAIN_1, ADS1220_GAIN_2, ADS1220_GAIN_4, ADS1220_GAIN_8};
        const char *gain_names[] = {"1x", "2x", "4x", "8x"};

        for (int i = 0; i < 4; i++) {
            ads1220_set_gain(&ads1220_device, gains[i]);
            ads1220_set_compare_channels(&ads1220_device, ADS1220_MUX_0_1);

            int32_t raw_data = ads1220_get_raw_data(&ads1220_device);
            float voltage = ads1220_get_voltage_mv(&ads1220_device) / 1000.0f;

            LOG_INF("Gain %s: %d (%.6fV)", gain_names[i], raw_data, (double)voltage);
            k_msleep(100);
        }

        // Reset to default gain
        ads1220_set_gain(&ads1220_device, ADS1220_GAIN_1);

        k_msleep(3000); // Wait 3 seconds before next cycle
    }
}

void adc_app_start(void) {
    int ret = adc_init();
    if (ret < 0) {
        LOG_ERR("ADS1220_WE ADC initialization failed: %d", ret);
        return;
    }

    LOG_INF("ADS1220_WE ADC application started");
    adc_test_loop();
}