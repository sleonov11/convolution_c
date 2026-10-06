#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

int main (int argc, char* argv[]) {
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

    FILE* in = fopen(input_path, "rb");
    if (in == NULL) {
        perror("входной файл не открылся");
        return 1;
    }

    FILE* out = fopen(output_path, "wb");
    if (out == NULL) {
        fclose(in);
        perror("выходной файл на открылся");
        return 1;
    }

    printf("files are opened!!!\n");

    uint32_t H = 0;
    uint32_t W = 0;
    if ((fread(&H, sizeof(uint32_t), 1, in) != 1) || (fread(&W, sizeof(uint32_t), 1, in) != 1)) {
        perror("ошибка чтения H или W");
        fclose(in);
        fclose(out);
        return 1;
    }

    size_t full_size = (size_t)H*W;
    uint8_t* A = malloc(full_size);
    uint8_t* B = malloc(full_size);
    uint8_t* C = malloc(full_size);
    if (A == NULL || B == NULL || C == NULL) {
        perror("ошибка выделения памяти");
        free(A); free(B); free(C);
        fclose(in); fclose(out);
        return 1;
    }

    for (size_t i = 0; i < full_size; ++i) {
        if  ((fread(&A[i], sizeof(uint8_t), 1, in) != 1) ||
            (fread(&B[i], sizeof(uint8_t), 1, in)!= 1) ||
            (fread(&C[i], sizeof(uint8_t), 1, in) != 1)) {
                perror("ошибка чтения матриц A B C");
                free(A); free(B); free(C);
                fclose(in); fclose(out);
                return 1;
            }
    }

    uint16_t DH = 0;
    uint16_t DW = 0;
    if (fread(&DH, sizeof(uint16_t), 1, in) != 1 || fread(&DW, sizeof(uint16_t), 1, in)!=1) {
        perror("не получилось считать dw или dh");
        free(A); free(B); free(C);
        fclose(in); fclose(out);
        return 1;
    }

    size_t D_size = (size_t)DH * DW;
    int8_t* D = malloc(D_size);
    if (!D) {
        perror("не получилось выделить память под D ((");
        free(A); free(B); free(C); free(D);
        fclose(in); fclose(out);
        return 1;
    }

    
    for (size_t i = 0; i < D_size; ++i) {
        if (fread(&D[i], sizeof(int8_t), 1, in) != 1) {
            perror("Ошибка чтения D");
            free(A); free(B); free(C); free(D);
            fclose(in); fclose(out);
            return 1;
        }
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

    uint8_t* A_padded = calloc(padded_size, sizeof(uint8_t));
    uint8_t* B_padded = calloc(padded_size, sizeof(uint8_t));
    uint8_t* C_padded = calloc(padded_size, sizeof(uint8_t));
    
    if (!A_padded || !B_padded || !C_padded) {
        perror("ошибка выделения памяти под расширенные матрицы");
        free(A); free(B); free(C); free(D);
        free(A_padded); free(B_padded); free(C_padded);
        fclose(in); fclose(out);
        return 1;
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
    fclose(in);

    

    fclose(out);

    return 0;
}