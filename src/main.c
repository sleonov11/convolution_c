#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>


static int convolve_pixel (const uint8_t* padded, size_t padded_w,
                            const int8_t* D, size_t DH, size_t DW, size_t r, size_t c) {
    // если будет гигантская матрица и большое ядро int - не хватит. Нужен long long

    int res = 0;
    for (size_t di = 0; di < DH; ++di) {
        for (size_t dj = 0; dj < DW; ++dj) {
            int a = (int)padded[(r + di) * padded_w +(c + dj)];
            int b = (int)D[di * DW + dj];
            res += a * b;
        }
    }
    return res;
}

static uint8_t process_value(int val) {
    val = val < 0 ? -(val % 241) : (val > 255 ? val % 251 : val);
    return (uint8_t)val;
}

int main (int argc, char* argv[]) {
    // инициализация для cleanup
    FILE* in = NULL;
    FILE* out = NULL;
    uint8_t *A = NULL, *B = NULL, *C = NULL;
    uint8_t *buf = NULL;
    int8_t* D = NULL;
    uint8_t *A_padded = NULL, *B_padded = NULL, *C_padded = NULL;
    uint8_t* out_buf = NULL;
    int ret = 1;
    
    char* input_path = NULL;
    char* output_path = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "i:o:")) != -1) {
        switch (opt) {
            case 'i' :
                input_path = optarg;
                break;
            case 'o' :
                output_path = optarg;
                break;
            default :
                return 10;
        }
    }
    if (input_path == NULL || output_path == NULL) return 10;

    in = fopen(input_path, "rb");
    if (in == NULL) {
        perror("входной файл не открылся");
        goto cleanup;
    }

    out = fopen(output_path, "wb");
    if (out == NULL) {
        perror("выходной файл на открылся");
        goto cleanup;
    }

    uint32_t H = 0;
    uint32_t W = 0;
    if ((fread(&H, sizeof(uint32_t), 1, in) != 1) || (fread(&W, sizeof(uint32_t), 1, in) != 1)) {
        perror("ошибка чтения H или W");
        goto cleanup;
    }

    size_t full_size = (size_t)H*W;

    A = malloc(full_size);
    B = malloc(full_size);
    C = malloc(full_size);
    if (A == NULL || B == NULL || C == NULL) {
        perror("ошибка выделения памяти");
        goto cleanup;
    }

    size_t buf_size = 3 * full_size;
    buf = malloc(buf_size);
    if (!buf) {
        perror("ошибка выделения буфера");
        goto cleanup;
    }
    if (fread(buf, 1, buf_size, in) != buf_size) {
        perror("ошибка чтения A B C");
        goto cleanup;
    }

    for (size_t i = 0; i < full_size; ++i) {
        A[i] = buf[3*i + 0];
        B[i] = buf[3*i + 1];
        C[i] = buf[3*i + 2];
    }
    free(buf);
    buf = NULL;

    uint16_t DH = 0;
    uint16_t DW = 0;
    if (fread(&DH, sizeof(uint16_t), 1, in) != 1 || fread(&DW, sizeof(uint16_t), 1, in)!=1) {
        perror("не получилось считать dw или dh");
        goto cleanup;
    }

    size_t D_size = (size_t)DH * DW;
    D = malloc(D_size);
    if (!D) {
        perror("не получилось выделить память под D ((");
        goto cleanup;
    }
    
    if (fread(D, sizeof(int8_t), D_size, in) != D_size) {
        perror("Ошибка чтения D");
        goto cleanup;
    }

    // Было 2 идеи. 1 - каждый раз проверять элемент на выход за пределы и заменять 0;
    // 2 - заранее сделать паддинг исходных матриц, и ходить по ним без проврок.
    // Я посчитал 2 вариант предпочтительней, хотя будет больше потребления памяти, т.к.
    // свертка во 2 варианте будет считаться куда быстрее, при больших матрицах.
    // + 2ой вариант удачно векторизуется из-за отсутствия ветвлений в горячих циклах!

    size_t pad_h = DH / 2;
    size_t pad_w = DW / 2;
    size_t padded_H = (size_t)H + 2 * pad_h;
    size_t padded_W = (size_t)W + 2 * pad_w;
    size_t padded_size = padded_H * padded_W;

    A_padded = calloc(padded_size, sizeof(uint8_t));
    B_padded = calloc(padded_size, sizeof(uint8_t));
    C_padded = calloc(padded_size, sizeof(uint8_t));
    
    if (!A_padded || !B_padded || !C_padded) {
        perror("ошибка выделения памяти под расширенные матрицы");
        goto cleanup;
    }

    for (uint32_t i = 0; i < H; ++i) {
        for (uint32_t j = 0; j < W; ++j) {
            size_t src_idx = i * W + j;
            size_t dst_idx = (i + pad_h) * padded_W + (j + pad_w);
            A_padded[dst_idx] = A[src_idx];
            B_padded[dst_idx] = B[src_idx];
            C_padded[dst_idx] = C[src_idx];
        }
    }

    free(A); free(B); free(C);
    A = B = C = NULL;
    fclose(in);
    in = NULL;

    out_buf = malloc(buf_size);
    if (!out_buf) {
        perror("ошибка выделения выходного буфера");
        goto cleanup;
    }
    
    size_t idx = 0;
    for (size_t i = 0; i < H; ++i) {
        for (size_t j = 0; j < W; ++j) {
            int a = convolve_pixel(A_padded, padded_W, D, DH, DW, i, j);
            int b = convolve_pixel(B_padded, padded_W, D, DH, DW, i, j);
            int c = convolve_pixel(C_padded, padded_W, D, DH, DW, i, j);

            out_buf[idx++] = process_value(a);
            out_buf[idx++] = process_value(b);
            out_buf[idx++] = process_value(c);
        }
    }

    if ((fwrite(&H, sizeof(uint32_t), 1, out) != 1) ||
         (fwrite(&W, sizeof(uint32_t), 1, out) != 1) ||
         (fwrite(out_buf, sizeof(uint8_t), buf_size, out) != buf_size)) {
            perror("ошибка записи в выходной файл");
            goto cleanup;
        }

    ret = 0;
    cleanup:
        free(A); free(B); free(C);
        free(buf);
        free(D);
        free(A_padded); free(B_padded); free(C_padded);
        free(out_buf);
        if (in) fclose(in);
        if (out) fclose(out);
        return ret;
}