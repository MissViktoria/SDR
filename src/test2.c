#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <complex.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Параметры сигнала
#define SYMBOL_RATE 100000.0       // tx speed
#define SAMP_RATE   1000000.0      // discret freq
#define UPSAMPLE    (int)(SAMP_RATE / SYMBOL_RATE) // upsample coef
#define SPAN        8              // filter length
#define TAPS        (SPAN * UPSAMPLE) // filter coef amount
#define IF_FREQ     100000.0       // part freq 

// CS16
typedef struct {
    float i;
    float q;
} complex_float_t;

int main(void)
{
    const char *msg = "hello world!";
    size_t msg_len = strlen(msg);

    size_t num_bits = msg_len * 8;
    uint8_t *bits = (uint8_t *)malloc(num_bits);

    for (size_t i = 0; i < msg_len; i++) {
        for (int b = 0; b < 8; b++) {
            bits[i * 8 + b] = (msg[i] >> (7 - b)) & 1;
        }
    }

    size_t num_symbols = num_bits / 2;
    complex_float_t *symbols = (complex_float_t *)malloc(num_symbols * sizeof(complex_float_t));

    for (size_t i = 0; i < num_symbols; i++) {
        uint8_t bit1 = bits[2 * i];
        uint8_t bit2 = bits[2 * i + 1];

        // 00 -> +1 + j1, 01 -> -1 + j1, 10 -> +1 - j1, 11 -> -1 - j1
        symbols[i].i = (bit1 == 0) ? 1.0 : -1.0;
        symbols[i].q = (bit2 == 0) ? 1.0 : -1.0;
    }

    // Апсемплинг (Вставка нулей)
    size_t upsampled_len = num_symbols * UPSAMPLE;
    complex_float_t *upsampled = (complex_float_t *)calloc(upsampled_len, sizeof(complex_float_t));

    for (size_t i = 0; i < num_symbols; i++) {
        upsampled[i * UPSAMPLE] = symbols[i];
    }

    // Свертка (Формирующий фильтр Root-Raised Cosine - RRC)
    // Генерируем коэффициенты фильтра (TAPS)
    float *taps = (float *)malloc(TAPS * sizeof(float));
    float alpha = 0.35; // Коэффициент сглаживания (rolloff)
    float sum_taps = 0.0;

    for (int i = 0; i < TAPS; i++) {
        float t = (float)(i - TAPS / 2) / UPSAMPLE;

        if (fabs(t) < 1e-6) {
            taps[i] = 1.0 - alpha + (4.0 * alpha / M_PI);
        } else if (fabs(fabs(t) - 1.0 / (4.0 * alpha)) < 1e-6) {
            float term1 = (1.0 + 2.0 / M_PI) * sin(M_PI / (4.0 * alpha));
            float term2 = (1.0 - 2.0 / M_PI) * cos(M_PI / (4.0 * alpha));
            taps[i] = (alpha / sqrt(2.0)) * (term1 + term2);
        } else {
            float num = sin(M_PI * t * (1.0 - alpha)) + 4.0 * alpha * t * cos(M_PI * t * (1.0 + alpha));
            float den = M_PI * t * (1.0 - pow(4.0 * alpha * t, 2.0));
            taps[i] = num / den;
        }
        sum_taps += taps[i];
    }

    for (int i = 0; i < TAPS; i++) taps[i] /= sum_taps;

    complex_float_t *tx_baseband = (complex_float_t *)calloc(upsampled_len + TAPS, sizeof(complex_float_t));

    for (size_t i = 0; i < upsampled_len; i++) {
        if (upsampled[i].i == 0.0 && upsampled[i].q == 0.0) continue;

        for (int j = 0; j < TAPS; j++) {
            tx_baseband[i + j].i += upsampled[i].i * taps[j];
            tx_baseband[i + j].q += upsampled[i].q * taps[j];
        }
    }

    // Сдвиг по частоте(ПЧ) и перевод В CS16 (int16)
    size_t total_samples = upsampled_len + TAPS;
    int16_t *tx_cs16 = (int16_t *)malloc(2 * total_samples * sizeof(int16_t));

    float max_amplitude = 0.0;
    // Находим пик, чтобы отмасштабировать в int16
    for (size_t i = 0; i < total_samples; i++) {
        if (fabs(tx_baseband[i].i) > max_amplitude) max_amplitude = fabs(tx_baseband[i].i);
        if (fabs(tx_baseband[i].q) > max_amplitude) max_amplitude = fabs(tx_baseband[i].q);
    }
    float scale = 20000.0 / (max_amplitude > 0 ? max_amplitude : 1.0);

    for (size_t i = 0; i < total_samples; i++) {
        // Сдвиг на промежуточную частоту (IF_FREQ)
        float phase = 2.0 * M_PI * IF_FREQ * i / SAMP_RATE;
        float shifted_i = tx_baseband[i].i * cos(phase) - tx_baseband[i].q * sin(phase);
        float shifted_q = tx_baseband[i].i * sin(phase) + tx_baseband[i].q * cos(phase);

        tx_cs16[2 * i]     = (int16_t)(shifted_i * scale);
        tx_cs16[2 * i + 1] = (int16_t)(shifted_q * scale);
    }

    printf("Сгенерировано %zu сэмплов для передачи.\n", total_samples);

    // tx
    SoapySDRKwargs args_tx = {};
    SoapySDRKwargs_set(&args_tx, "driver", "plutosdr");
    SoapySDRKwargs_set(&args_tx, "uri", "usb:1.10.5");
    SoapySDRDevice *sdr_tx = SoapySDRDevice_make(&args_tx);
    SoapySDRKwargs_clear(&args_tx);

    if (sdr_tx == NULL) {
        printf("SoapySDRDevice_make (TX) fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    SoapySDRDevice_setSampleRate(sdr_tx, SOAPY_SDR_TX, 0, SAMP_RATE);
    SoapySDRDevice_setFrequency(sdr_tx, SOAPY_SDR_TX, 0, 800e6, NULL);
    SoapySDRDevice_setGain(sdr_tx, SOAPY_SDR_TX, 0, 0.0); 

    size_t tx_channels[] = {0};
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(
        sdr_tx, SOAPY_SDR_TX, SOAPY_SDR_CS16, tx_channels, 1, NULL);

    if (txStream == NULL) {
        printf("setupStream tx fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr_tx);
        return EXIT_FAILURE;
    }

    SoapySDRDevice_activateStream(sdr_tx, txStream, 0, 0, 0);
    printf("Начинаем передачу...\n");

    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr_tx, txStream);
    size_t offset = 0;

    // Повторим передачу 5 раз, чтобы приемник точно её поймал
    for (int repeat = 0; repeat < 5; repeat++) {
        offset = 0;
        while (offset < total_samples) {
            size_t chunk = total_samples - offset;
            if (chunk > tx_mtu) chunk = tx_mtu;

            void *buffs[] = {&tx_cs16[2 * offset]};
            int flags = 0;
            int ret = SoapySDRDevice_writeStream(sdr_tx, txStream, (const void * const *)buffs, chunk, &flags, 0, 1000000);

            if (ret < 0) {
                printf("Ошибка записи TX: %i\n", ret);
                break;
            }
            offset += ret;
        }
        printf("Пакет %d передан.\n", repeat + 1);
    }

    // Очистка ресурсов
    SoapySDRDevice_deactivateStream(sdr_tx, txStream, 0, 0);
    SoapySDRDevice_closeStream(sdr_tx, txStream);
    SoapySDRDevice_unmake(sdr_tx);

    free(bits);
    free(symbols);
    free(upsampled);
    free(taps);
    free(tx_baseband);
    free(tx_cs16);

    printf("Добрый день, вас приветствует капитан корабля. hellow world прилетел на станцию назначения!\n");
    return EXIT_SUCCESS;
}
