#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define FIRST_CHAR 32u
#define GLYPH_COUNT 95u
#define FONT_HEADER_BYTES 16u
#define SIZE_HEADER_BYTES 20u
#define GLYPH_RECORD_BYTES 20u

struct glyph {
  uint16_t width;
  uint16_t height;
  int16_t bearing_x;
  int16_t bearing_y;
  int16_t advance;
  uint8_t *bitmap;
  uint32_t bitmap_bytes;
};

static void
write_be16(FILE *file, uint16_t value)
{
  uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
  fwrite(bytes, 1, sizeof(bytes), file);
}

static void
write_be16_signed(FILE *file, int16_t value)
{
  write_be16(file, (uint16_t)value);
}

static void
write_be32(FILE *file, uint32_t value)
{
  uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                      (uint8_t)(value >> 8), (uint8_t)value};
  fwrite(bytes, 1, sizeof(bytes), file);
}

static int
parse_sizes(const char *text, uint16_t **sizes_out, size_t *count_out)
{
  char *copy;
  char *token;
  char *end;
  uint16_t *sizes = NULL;
  size_t count = 0;
  size_t capacity = 0;

  copy = strdup(text);
  if (copy == NULL) return 0;
  token = strtok(copy, ",");
  while (token != NULL) {
    unsigned long parsed = strtoul(token, &end, 10);
    if (*token == '\0' || *end != '\0' || parsed == 0 || parsed > UINT16_MAX) {
      free(sizes);
      free(copy);
      return 0;
    }
    if (count == capacity) {
      size_t next_capacity = capacity == 0 ? 8u : capacity * 2u;
      uint16_t *next = (uint16_t *)realloc(sizes,
                                           next_capacity * sizeof(*sizes));
      if (next == NULL) {
        free(sizes);
        free(copy);
        return 0;
      }
      sizes = next;
      capacity = next_capacity;
    }
    sizes[count++] = (uint16_t)parsed;
    token = strtok(NULL, ",");
  }
  free(copy);
  if (count == 0) {
    free(sizes);
    return 0;
  }
  *sizes_out = sizes;
  *count_out = count;
  return 1;
}

static int
clamp_int16(int value, int16_t *result)
{
  if (value < INT16_MIN || value > INT16_MAX) return 0;
  *result = (int16_t)value;
  return 1;
}

static int
rasterize_size(FT_Face face, uint16_t pixel_size, struct glyph *glyphs,
               uint16_t *line_height, int16_t *ascender)
{
  FT_Error error;
  int metric_line_height;
  int metric_ascender;

  error = FT_Set_Pixel_Sizes(face, 0, pixel_size);
  if (error != 0) return 0;
  metric_line_height = (int)((face->size->metrics.height + 32) >> 6);
  metric_ascender = (int)((face->size->metrics.ascender + 32) >> 6);
  if (metric_line_height <= 0 || metric_line_height > UINT16_MAX ||
      !clamp_int16(metric_ascender, ascender)) {
    return 0;
  }
  *line_height = (uint16_t)metric_line_height;

  for (uint16_t index = 0; index < GLYPH_COUNT; index++) {
    FT_UInt character = FIRST_CHAR + index;
    FT_GlyphSlot slot;
    int advance;
    error = FT_Load_Char(face, character, FT_LOAD_RENDER);
    if (error != 0) return 0;
    slot = face->glyph;
    if (slot->bitmap.width > UINT16_MAX || slot->bitmap.rows > UINT16_MAX) {
      return 0;
    }
    glyphs[index].width = (uint16_t)slot->bitmap.width;
    glyphs[index].height = (uint16_t)slot->bitmap.rows;
    if (!clamp_int16(slot->bitmap_left, &glyphs[index].bearing_x) ||
        !clamp_int16(slot->bitmap_top, &glyphs[index].bearing_y)) {
      return 0;
    }
    advance = (int)((slot->advance.x + 32) >> 6);
    if (!clamp_int16(advance, &glyphs[index].advance)) return 0;
    glyphs[index].bitmap_bytes = (uint32_t)slot->bitmap.width *
      (uint32_t)slot->bitmap.rows;
    glyphs[index].bitmap = NULL;
    if (glyphs[index].bitmap_bytes == 0) continue;
    glyphs[index].bitmap = (uint8_t *)malloc(glyphs[index].bitmap_bytes);
    if (glyphs[index].bitmap == NULL) return 0;
    for (uint16_t row = 0; row < glyphs[index].height; row++) {
      int source_row = slot->bitmap.pitch >= 0 ? row :
        glyphs[index].height - row - 1;
      const uint8_t *source = slot->bitmap.buffer +
        (size_t)source_row * (size_t)(slot->bitmap.pitch >= 0
                                        ? slot->bitmap.pitch
                                        : -slot->bitmap.pitch);
      memcpy(glyphs[index].bitmap +
               (size_t)row * glyphs[index].width,
             source, glyphs[index].width);
    }
  }
  return 1;
}

static void
free_glyphs(struct glyph *glyphs)
{
  for (uint16_t index = 0; index < GLYPH_COUNT; index++) {
    free(glyphs[index].bitmap);
    glyphs[index].bitmap = NULL;
  }
}

int
main(int argc, char **argv)
{
  FT_Library library = NULL;
  FT_Face face = NULL;
  uint16_t *sizes = NULL;
  size_t size_count = 0;
  struct glyph *all_glyphs = NULL;
  uint16_t *line_heights = NULL;
  int16_t *ascenders = NULL;
  uint64_t records_base;
  uint64_t bitmap_base;
  uint64_t file_size;
  uint64_t bitmap_cursor;
  FILE *output = NULL;
  int result = 1;

  if (argc != 4) {
    fprintf(stderr, "usage: %s INPUT.ttf OUTPUT.pbf SIZE[,SIZE...]\n", argv[0]);
    return 2;
  }
  if (!parse_sizes(argv[3], &sizes, &size_count) || size_count > UINT16_MAX) {
    fprintf(stderr, "invalid font sizes: %s\n", argv[3]);
    goto cleanup;
  }
  if (FT_Init_FreeType(&library) != 0 ||
      FT_New_Face(library, argv[1], 0, &face) != 0) {
    fprintf(stderr, "cannot open TrueType font: %s\n", argv[1]);
    goto cleanup;
  }

  all_glyphs = (struct glyph *)calloc(size_count * GLYPH_COUNT,
                                      sizeof(*all_glyphs));
  line_heights = (uint16_t *)calloc(size_count, sizeof(*line_heights));
  ascenders = (int16_t *)calloc(size_count, sizeof(*ascenders));
  if (all_glyphs == NULL || line_heights == NULL || ascenders == NULL) {
    fprintf(stderr, "out of memory while preparing font\n");
    goto cleanup;
  }
  for (size_t size_index = 0; size_index < size_count; size_index++) {
    if (!rasterize_size(face, sizes[size_index],
                        all_glyphs + size_index * GLYPH_COUNT,
                        &line_heights[size_index], &ascenders[size_index])) {
      fprintf(stderr, "cannot rasterize font size %u\n", sizes[size_index]);
      goto cleanup;
    }
  }

  records_base = FONT_HEADER_BYTES + size_count * SIZE_HEADER_BYTES;
  bitmap_base = records_base + size_count * GLYPH_COUNT * GLYPH_RECORD_BYTES;
  bitmap_cursor = bitmap_base;
  for (size_t size_index = 0; size_index < size_count; size_index++) {
    for (uint16_t glyph_index = 0; glyph_index < GLYPH_COUNT; glyph_index++) {
      bitmap_cursor += all_glyphs[size_index * GLYPH_COUNT + glyph_index].bitmap_bytes;
    }
  }
  file_size = bitmap_cursor;
  if (file_size > UINT32_MAX) {
    fprintf(stderr, "bitmap font is too large\n");
    goto cleanup;
  }
  output = fopen(argv[2], "wb");
  if (output == NULL) {
    fprintf(stderr, "cannot create %s: %s\n", argv[2], strerror(errno));
    goto cleanup;
  }

  fwrite("PBF1", 1, 4, output);
  write_be16(output, FIRST_CHAR);
  write_be16(output, GLYPH_COUNT);
  write_be16(output, (uint16_t)size_count);
  write_be16(output, 0);
  write_be32(output, (uint32_t)file_size);
  bitmap_cursor = bitmap_base;
  for (size_t size_index = 0; size_index < size_count; size_index++) {
    write_be16(output, sizes[size_index]);
    write_be16(output, line_heights[size_index]);
    write_be16_signed(output, ascenders[size_index]);
    write_be16_signed(output, 0);
    write_be16(output, GLYPH_COUNT);
    write_be16(output, 0);
    write_be32(output, (uint32_t)(records_base +
                                  size_index * GLYPH_COUNT * GLYPH_RECORD_BYTES));
    write_be32(output, (uint32_t)bitmap_cursor);
    for (uint16_t glyph_index = 0; glyph_index < GLYPH_COUNT; glyph_index++) {
      struct glyph *glyph = &all_glyphs[size_index * GLYPH_COUNT + glyph_index];
      write_be16(output, glyph->width);
      write_be16(output, glyph->height);
      write_be16_signed(output, glyph->bearing_x);
      write_be16_signed(output, glyph->bearing_y);
      write_be16_signed(output, glyph->advance);
      write_be16(output, 0);
      write_be32(output, (uint32_t)bitmap_cursor);
      write_be32(output, glyph->bitmap_bytes);
      bitmap_cursor += glyph->bitmap_bytes;
    }
  }
  for (size_t size_index = 0; size_index < size_count; size_index++) {
    for (uint16_t glyph_index = 0; glyph_index < GLYPH_COUNT; glyph_index++) {
      struct glyph *glyph = &all_glyphs[size_index * GLYPH_COUNT + glyph_index];
      if (glyph->bitmap_bytes > 0) {
        fwrite(glyph->bitmap, 1, glyph->bitmap_bytes, output);
      }
    }
  }
  if (ferror(output) || fclose(output) != 0) {
    output = NULL;
    fprintf(stderr, "cannot write %s\n", argv[2]);
    goto cleanup;
  }
  output = NULL;
  result = 0;

cleanup:
  if (output != NULL) fclose(output);
  if (all_glyphs != NULL) {
    for (size_t size_index = 0; size_index < size_count; size_index++) {
      free_glyphs(all_glyphs + size_index * GLYPH_COUNT);
    }
  }
  free(all_glyphs);
  free(line_heights);
  free(ascenders);
  free(sizes);
  if (face != NULL) FT_Done_Face(face);
  if (library != NULL) FT_Done_FreeType(library);
  return result;
}
