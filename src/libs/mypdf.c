#include "mypdf.h"
#include "mupdf/fitz.h"
#include "libimagequant.h"
#include "lodepng.h"
#include "lodepng.c"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int png_compress(const char * input_png_file_path)
{
    unsigned int width, height;
    unsigned char *raw_rgba_pixels;
    unsigned int status = lodepng_decode32_file(&raw_rgba_pixels, &width, &height, input_png_file_path);
    if (status) {
        fprintf(stderr, "Can't load %s: %s\n", input_png_file_path, lodepng_error_text(status));
        return 1;
    }

    liq_attr *handle = liq_attr_create();
    liq_image *input_image = liq_image_create_rgba(handle, raw_rgba_pixels, width, height, 0);

    liq_result *quantization_result;
    if (liq_image_quantize(input_image, handle, &quantization_result) != LIQ_OK) {
        fprintf(stderr, "Quantization failed\n");
        free(raw_rgba_pixels);
        return 1;
    }

    size_t pixels_size = width * height;
    unsigned char *raw_8bit_pixels = malloc(pixels_size);
    liq_set_dithering_level(quantization_result, 1.0);

    liq_write_remapped_image(quantization_result, input_image, raw_8bit_pixels, pixels_size);
    const liq_palette *palette = liq_get_palette(quantization_result);

    LodePNGState state;
    lodepng_state_init(&state);
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8;

    for(int i=0; i < palette->count; i++) {
        lodepng_palette_add(&state.info_png.color, palette->entries[i].r, palette->entries[i].g, palette->entries[i].b, palette->entries[i].a);
        lodepng_palette_add(&state.info_raw, palette->entries[i].r, palette->entries[i].g, palette->entries[i].b, palette->entries[i].a);
    }

    unsigned char *output_file_data;
    size_t output_file_size;
    unsigned int out_status = lodepng_encode(&output_file_data, &output_file_size, raw_8bit_pixels, width, height, &state);
    if (out_status) {
        fprintf(stderr, "Can't encode image: %s\n", lodepng_error_text(out_status));
        free(raw_8bit_pixels);
        return 1;
    }

    char * output_png_file_path;
    output_png_file_path = malloc(sizeof(char) * (strlen(input_png_file_path)  + 3));

    strncpy(output_png_file_path, input_png_file_path, strlen(input_png_file_path) - 4);
    strcpy(output_png_file_path + strlen(input_png_file_path) - 4, "_c.png");
    output_png_file_path[strlen(input_png_file_path) + 2] = '\0';

    FILE *fp = fopen(output_png_file_path, "wb");
    if (!fp) {
        fprintf(stderr, "Unable compress to write to %s\n", output_png_file_path);
        free(output_png_file_path);
        free(raw_8bit_pixels);
        free(output_file_data);
        free(raw_rgba_pixels);
        return 1;
    }
    fwrite(output_file_data, 1, output_file_size, fp);
    fclose(fp);

    liq_result_destroy(quantization_result);
    liq_image_destroy(input_image);
    liq_attr_destroy(handle);

    free(raw_8bit_pixels);
    lodepng_state_cleanup(&state);

    //覆盖默认
    rename(output_png_file_path, input_png_file_path);

    free(raw_rgba_pixels);
    free(output_png_file_path);
    free(output_file_data);

    return 0;
}

int mypdf_size(const char * filename)
{
    int _size = 0;
    fz_context *_ctx;
    fz_document *_doc;

    _ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!_ctx) {
        return _size;
    }

    fz_try(_ctx) {
        fz_register_document_handlers(_ctx);
        _doc = fz_open_document(_ctx, filename);
        if (fz_needs_password(_ctx, _doc)) {
            fz_drop_document(_ctx, _doc);
            fz_drop_context(_ctx);

            return _size;
        }

        _size = fz_count_pages(_ctx, _doc);

        fz_drop_document(_ctx, _doc);
        fz_drop_context(_ctx);
    }
    fz_catch(_ctx) {
       return 0;
    }

    return _size;
}

int mypdf_parse(const char *filename, int zoom, int start, int end, int compress)
{
    fz_context *_ctx;
    fz_document *_doc;
    int _size = 0;

    _ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!_ctx) {
        return 1;
    }

    fz_try(_ctx) {
        fprintf(stdout, "analyze pdf=%s total page=%d\n", filename, _size);

        fz_register_document_handlers(_ctx);
        _doc = fz_open_document(_ctx, filename);
        _size = fz_count_pages(_ctx, _doc);

        if (end == 0 || end > _size) {
            end = _size;
        }
        if (start <= 0 || start > end) {
            start = 1;
        }

        int width = log10(end) + 1;
        fz_matrix transform;
        transform = fz_scale(zoom, zoom);

        fz_pixmap *pix;

        //单页文件名
        int len;
        char buf[10];
        char *_image_name;
        _image_name = malloc(sizeof(char) * (strlen(filename)  + width + 2));

        //解析
        for (int i = start; i <= end; ++i) {
            sprintf(buf, "_%d.png", i);

            strncpy(_image_name, filename, strlen(filename) - 4);
            strcpy(_image_name + strlen(filename) - 4, buf);
            len = strlen(filename) - 4 + strlen(buf);
            _image_name[len] = '\0';

            fprintf(stdout, "len=%d, %s|\n", len, _image_name);

            fz_try(_ctx) {
                pix = fz_new_pixmap_from_page_number(_ctx, _doc, i - 1, transform, fz_device_rgb(_ctx), 0);
                fz_save_pixmap_as_png(_ctx, pix, _image_name);
                fz_drop_pixmap(_ctx, pix);
            }
            fz_catch(_ctx) {
                continue;
            }

            //压缩
            if (compress) {
                png_compress(_image_name);
            }
        }

        //清理
        free(_image_name);
        fz_drop_document(_ctx, _doc);
        fz_drop_context(_ctx);

        return 0;
    }
    fz_catch(_ctx) {
        return 1;
    }

    return 1;
}
