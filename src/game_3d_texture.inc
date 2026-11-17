/* Experimental opt-in texture path. The established flat renderer is unchanged.
 * Included after game_platform.c so transforms, depth and display stay shared. */
struct game_3d_uv_vertex {
  struct game_3d_vertex p;
  float u, v;
};

static int
game_3d_clip_uv(const struct game_3d_uv_vertex *input, int count,
                float plane, bool greater, struct game_3d_uv_vertex *output)
{
  int written = 0;
  for (int i = 0; i < count; i++) {
    const struct game_3d_uv_vertex *a = &input[i], *b = &input[(i + 1) % count];
    bool ai = greater ? a->p.z >= plane : a->p.z <= plane;
    bool bi = greater ? b->p.z >= plane : b->p.z <= plane;
    if (ai != bi) {
      float t = (plane - a->p.z) / (b->p.z - a->p.z);
      output[written++] = (struct game_3d_uv_vertex){
        {a->p.x + (b->p.x - a->p.x) * t,
         a->p.y + (b->p.y - a->p.y) * t, plane},
        a->u + (b->u - a->u) * t, a->v + (b->v - a->v) * t};
    }
    if (bi) output[written++] = *b;
  }
  return written;
}

static void
game_3d_raster_uv(const game_texture_t *texture,
                  const struct game_3d_uv_vertex *a,
                  const struct game_3d_uv_vertex *b,
                  const struct game_3d_uv_vertex *c, bool perspective)
{
  struct game_3d_projected_vertex p[3] = {
    game_3d_project_vertex(&a->p), game_3d_project_vertex(&b->p),
    game_3d_project_vertex(&c->p)};
  float area = game_3d_edge(p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y);
  if (!isfinite(area) || area <= 0.000001f) return;
  float min_x = fminf(p[0].x, fminf(p[1].x, p[2].x));
  float max_x = fmaxf(p[0].x, fmaxf(p[1].x, p[2].x));
  float min_y = fminf(p[0].y, fminf(p[1].y, p[2].y));
  float max_y = fmaxf(p[0].y, fmaxf(p[1].y, p[2].y));
  if (max_x < 0 || max_y < 0 || min_x >= game_3d_internal_width ||
      min_y >= game_3d_internal_height) return;
  int left = (int)fmaxf(0, floorf(min_x));
  int right = (int)fminf(game_3d_internal_width - 1, ceilf(max_x));
  int top = (int)fmaxf(0, floorf(min_y));
  int bottom = (int)fminf(game_3d_internal_height - 1, ceilf(max_y));
  float row[3], dx[3], dy[3], u[3], v[3];
  const struct game_3d_uv_vertex *vertices[] = {a, b, c};
  for (int i = 0; i < 3; i++) {
    int j = (i + 1) % 3, k = (i + 2) % 3;
    row[i] = game_3d_edge(p[j].x, p[j].y, p[k].x, p[k].y,
                          left + 0.5f, top + 0.5f) / area;
    dx[i] = (p[k].y - p[j].y) / area;
    dy[i] = (p[j].x - p[k].x) / area;
    u[i] = vertices[i]->u * (perspective ? p[i].inverse_z : 1.0f);
    v[i] = vertices[i]->v * (perspective ? p[i].inverse_z : 1.0f);
  }
  /* Interpolate u/z, v/z and 1/z once per row, then additions per sample. */
  float qdx = 0, udx = 0, vdx = 0;
  for (int i = 0; i < 3; i++) {
    qdx += dx[i] * p[i].inverse_z; udx += dx[i] * u[i]; vdx += dx[i] * v[i];
  }
  game_3d_add_dirty_rect(left, top, right, bottom);
  for (int y = top; y <= bottom; y++) {
    float w[3] = {row[0], row[1], row[2]}, q = 0, s = 0, t = 0;
    for (int i = 0; i < 3; i++) {
      q += w[i] * p[i].inverse_z; s += w[i] * u[i]; t += w[i] * v[i];
    }
    for (int x = left; x <= right; x++) {
      if (w[0] >= 0 && w[1] >= 0 && w[2] >= 0 && q > 0) {
        size_t offset = (size_t)y * game_3d_internal_width + x;
        uint16_t depth = game_3d_depth_value_fixed((int32_t)lrintf(q * GAME_3D_FIXED_ONE));
        if (depth > game_3d_depth_buffer[offset]) {
          float factor = perspective ? 1.0f / q : 1.0f;
          int tx = (int)(fmaxf(0, fminf(1, s * factor)) * texture->width);
          int ty = (int)(fmaxf(0, fminf(1, t * factor)) * texture->height);
          if (tx >= texture->width) tx = texture->width - 1;
          if (ty >= texture->height) ty = texture->height - 1;
          size_t texel = ((size_t)ty * texture->width + tx) * 2u;
          uint8_t high = texture->pixels[texel], low = texture->pixels[texel + 1];
          if ((((uint16_t)high << 8) | low) != texture->key_color) {
            game_3d_depth_buffer[offset] = depth;
            game_3d_write_pixel(x, y, high, low);
          }
        }
      }
      for (int i = 0; i < 3; i++) w[i] += dx[i];
      q += qdx; s += udx; t += vdx;
    }
    for (int i = 0; i < 3; i++) row[i] += dy[i];
  }
}

bool
game_3d_draw_textured_triangles(const game_texture_t *texture,
                                 const float *values, size_t triangles,
                                 bool perspective)
{
  if (!game_3d_frame_active || !game_3d_supported() || texture == NULL ||
      texture->pixels == NULL || texture->width == 0 || texture->height == 0 ||
      triangles > 256 || (triangles && values == NULL)) return false;
  /* Validate the complete batch before changing pixels. Bound input to keep
   * interpolation and projection in the useful experimental coordinate range. */
  for (size_t i = 0; i < triangles * 15; i++) {
    if (!isfinite(values[i]) || (i % 5 < 3 ? fabsf(values[i]) > 32768.0f :
        values[i] < 0 || values[i] > 1)) return false;
  }
  for (size_t face = 0; face < triangles; face++) {
    struct game_3d_uv_vertex input[8], temporary[8], clipped[8];
    for (int i = 0; i < 3; i++) {
      const float *v = values + face * 15 + i * 5;
      input[i] = (struct game_3d_uv_vertex){game_3d_transform_vertex(v[0], v[1], v[2]), v[3], v[4]};
    }
    int count = game_3d_clip_uv(input, 3, game_3d_camera.near_clip, true, temporary);
    count = game_3d_clip_uv(temporary, count, game_3d_camera.far_clip, false, clipped);
    for (int i = 1; i + 1 < count; i++) {
      game_3d_raster_uv(texture, &clipped[0], &clipped[i], &clipped[i + 1], perspective);
    }
  }
  return true;
}
