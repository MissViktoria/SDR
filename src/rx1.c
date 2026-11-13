#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <complex.h>
#include <time.h>

int main(void)
{
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", "usb:1.9.5");
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (sdr == NULL)
    {
        printf("SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, 1e6) != 0)
        printf("setSampleRate rx fail: %s\n", SoapySDRDevice_lastError());
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, 800e6, NULL) != 0)
        printf("setFrequency rx fail: %s\n", SoapySDRDevice_lastError());

    size_t channels[] = {0};
    size_t channel_count = 1;

    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (rxStream == NULL)
    {
        printf("setupStream rx fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    if (SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channels[0], 30.0) != 0)
        printf("setGain rx fail: %s\n", SoapySDRDevice_lastError());

    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    printf("MTU - RX: %lu\n", rx_mtu);

    int16_t *rx_buffer = (int16_t*)malloc(2 * rx_mtu * sizeof(int16_t));

    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0);
    printf("Start RX test (35 sec)...\n");
    const long timeoutUs = 400000;

    FILE *file = fopen("rxdata.pcm", "wb");
    if (file == NULL) {
        printf("Cannot open rxdata.pcm for writing\n");
        SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
        SoapySDRDevice_closeStream(sdr, rxStream);
        SoapySDRDevice_unmake(sdr);
        free(rx_buffer);
        return EXIT_FAILURE;
    }

    time_t start = time(NULL);
    size_t buffers_read = 0;
    size_t buffers_written = 0;
    size_t error_buffers = 0;

    while (time(NULL) - start < 35)
    {
        void *rx_buffs[] = {rx_buffer};
        int flags = 0;

        int sr = SoapySDRDevice_readStream(
            sdr, rxStream, rx_buffs, rx_mtu, &flags, NULL, timeoutUs);

        if (sr < 0) {
            continue;
        }

        if (flags < 0 || (flags & SOAPY_SDR_OVERFLOW) || (flags != 0 && flags != SOAPY_SDR_HAS_TIME)) {
            error_buffers++;
            if (error_buffers % 100 == 1) {
                printf("ВНИМАНИЕ: обнаружен сбойный буфер (flags=%d). Всего ошибок: %zu\n", flags, error_buffers);
            }
            buffers_read++;
            continue;
        }

        if (sr > 0) {
            size_t written = fwrite(rx_buffer, sr * 2 * sizeof(int16_t), 1, file);
            if (written == 1) {
                buffers_written++;
            }
        }
        buffers_read++;
    }

    fclose(file);

    printf("\nRX test complete!\n");
    printf("Всего прочитано буферов: %zu\n", buffers_read);
    printf("Записано в файл: %zu буферов\n", buffers_written);
    printf("Проигнорировано сбойных буферов: %zu\n", error_buffers);

    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_unmake(sdr);

    free(rx_buffer);
    return EXIT_SUCCESS;
}
