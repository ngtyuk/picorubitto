# 3D API example: a wireframe flight shooter using Game::ThreeD.
#
# This is a small original arcade scene: the player flies through a
# moving wireframe tunnel and shoots approaching fighters.
# It intentionally uses asset-free indexed meshes so the wireframe API can be
# exercised on both RP2350 and WASM with the same Ruby source.
require 'game'

BOX_VERTICES = [
  [-1.0, -1.0, -1.0], [1.0, -1.0, -1.0], [1.0, 1.0, -1.0],
  [-1.0, 1.0, -1.0], [-1.0, -1.0, 1.0], [1.0, -1.0, 1.0],
  [1.0, 1.0, 1.0], [-1.0, 1.0, 1.0]
]

BOX_EDGES = [
  0, 1, 1, 2, 2, 3, 3, 0,
  4, 5, 5, 6, 6, 7, 7, 4,
  0, 4, 1, 5, 2, 6, 3, 7
]

# A folded paper-airplane silhouette. The nose, wing tips, tail corners, and
# center ridge are deliberately separate so the shape reads at a distance.
FIGHTER_VERTICES = [
  [0.0, 0.16, 1.55],     # 0 nose
  [-1.30, -0.04, -0.75],  # 1 left wing tip
  [1.30, -0.04, -0.75],   # 2 right wing tip
  [-0.45, -0.02, -1.10],  # 3 left tail
  [0.45, -0.02, -1.10],   # 4 right tail
  [0.0, 0.02, -0.92],     # 5 tail center
  [0.0, 0.34, 0.30],      # 6 raised center ridge
  [-0.36, -0.10, -0.52],  # 7 left wing fold
  [0.36, -0.10, -0.52]    # 8 right wing fold
]

FIGHTER_EDGES = [
  0, 1, 1, 3, 3, 5, 5, 4, 4, 2, 2, 0,  # silhouette
  0, 6, 6, 5,                             # center ridge
  1, 7, 7, 6, 2, 8, 8, 6,                 # folded wings
  7, 3, 8, 4, 7, 8                        # tail folds
]

# A small elongated octahedron. Its front/back tips make the shot readable
# in depth as it travels away from the camera instead of becoming a single
# projected point.
SHOT_VERTICES = [
  [0.0, 0.0, 0.34],
  [0.22, 0.0, 0.0],
  [0.0, 0.22, 0.0],
  [-0.22, 0.0, 0.0],
  [0.0, -0.22, 0.0],
  [0.0, 0.0, -0.34]
]

SHOT_EDGES = [
  0, 1, 0, 2, 0, 3, 0, 4,
  5, 1, 5, 2, 5, 3, 5, 4,
  1, 2, 2, 3, 3, 4, 4, 1
]

PLAYER_VERTICES = FIGHTER_VERTICES
PLAYER_EDGES = FIGHTER_EDGES

ENEMY_STARTS = [
  [-3.5, 0.9, 16.0, 0.0], [2.8, 0.2, 21.0, 0.8],
  [-1.8, -0.6, 27.0, 1.6], [4.2, 1.1, 34.0, 2.4],
  [-4.5, -1.0, 41.0, 3.2], [0.0, 1.7, 48.0, 4.0],
  [3.2, -1.4, 55.0, 4.8], [-3.2, 0.0, 62.0, 5.6]
]

ROCK_STARTS = [
  [-3.0, -0.9, 12.0, 0.9, 0.8, 0.8, 0.25],
  [3.0, 1.0, 25.0, 1.2, 1.0, 1.2, -0.35],
  [0.0, -1.2, 42.0, 1.6, 1.4, 1.0, 0.55]
]

# Reverse winding keeps the faces visible from the camera's +Z flight path.
ROCK_TRIANGLES = [
  0, 2, 3, 0, 1, 2,  # rear face
  4, 6, 5, 4, 7, 6,  # front face
  0, 7, 4, 0, 3, 7,  # left face
  1, 6, 2, 1, 5, 6,  # right face
  0, 5, 1, 0, 4, 5,  # bottom face
  3, 6, 7, 3, 2, 6   # top face
]

ROCK_FACE_COLORS = [
  0x6b4d, 0x8410, 0x9cf3, 0x5aeb, 0x6b4d, 0x8410
]

STAR_COUNT = 24
RING_START_Z = [8.0, 16.0, 24.0, 32.0]
RING_LOOP_LENGTH = 32.0
SHOT_COUNT = 8
SHOT_SPEED_PER_SECOND = 42.0
SHOT_COOLDOWN_MS = 500
ROCK_HITS = 3
MAX_FRAME_DELTA_MS = 100
RETICLE_THICKNESS = 3
PLAYER_SCALE = 0.62
PLAYER_CENTER_Y_OFFSET = -1.35
PLAYER_CENTER_Z = 3.4
ENEMY_COUNT = ENEMY_STARTS.length
ROCK_COUNT = ROCK_STARTS.length

def append_scaled_translated_vertices(vertices, vertex_offset, model_vertices,
                                      scale_x, scale_y, scale_z,
                                      center_x, center_y, center_z)
  model_vertices.each do |vertex|
    vertices[vertex_offset] = center_x + vertex[0] * scale_x
    vertices[vertex_offset + 1] = center_y + vertex[1] * scale_y
    vertices[vertex_offset + 2] = center_z + vertex[2] * scale_z
    vertex_offset += 3
  end
  vertex_offset
end

def append_transformed_vertices(vertices, vertex_offset, model_vertices,
                                scale_x, scale_y, scale_z, yaw, pitch,
                                center_x, center_y, center_z, roll = 0.0)
  # The tunnel and inactive shots have no rotation. Avoid four trig calls and
  # the rotation arithmetic for these common cases.
  if yaw == 0.0 && pitch == 0.0 && roll == 0.0
    return append_scaled_translated_vertices(
      vertices, vertex_offset, model_vertices, scale_x, scale_y, scale_z,
      center_x, center_y, center_z
    )
  end

  yaw_sine = Math.sin(yaw)
  yaw_cosine = Math.cos(yaw)
  pitch_sine = Math.sin(pitch)
  pitch_cosine = Math.cos(pitch)
  roll_sine = 0.0
  roll_cosine = 1.0
  if roll != 0.0
    roll_sine = Math.sin(roll)
    roll_cosine = Math.cos(roll)
  end

  model_vertices.each do |vertex|
    x = vertex[0] * scale_x
    y = vertex[1] * scale_y
    z = vertex[2] * scale_z
    rotated_x = x * yaw_cosine - z * yaw_sine
    rotated_z = x * yaw_sine + z * yaw_cosine
    rotated_y = y * pitch_cosine - rotated_z * pitch_sine
    rotated_z = y * pitch_sine + rotated_z * pitch_cosine
    rolled_x = rotated_x * roll_cosine - rotated_y * roll_sine
    rolled_y = rotated_x * roll_sine + rotated_y * roll_cosine
    vertices[vertex_offset] = center_x + rolled_x
    vertices[vertex_offset + 1] = center_y + rolled_y
    vertices[vertex_offset + 2] = center_z + rotated_z
    vertex_offset += 3
  end
  vertex_offset
end

def append_model_vertices(vertices, vertex_offset, model_vertices,
                          scale_x, scale_y, scale_z, yaw, pitch,
                          center_x, center_y, center_z, roll = 0.0)
  append_transformed_vertices(
    vertices, vertex_offset, model_vertices, scale_x, scale_y, scale_z,
    yaw, pitch, center_x, center_y, center_z, roll
  )
end

def build_repeated_edges(edges, model_edges, model_vertex_count, instance_count,
                         vertex_base = 0, edge_base = 0)
  instance_count.times do |instance|
    base_index = vertex_base + instance * model_vertex_count
    edge_offset = edge_base + instance * model_edges.length
    edge_index = 0
    while edge_index < model_edges.length
      edges[edge_offset] = base_index + model_edges[edge_index]
      edges[edge_offset + 1] = base_index + model_edges[edge_index + 1]
      edge_offset += 2
      edge_index += 2
    end
  end
end

def reset_flight(enemy_x, enemy_y, enemy_z, enemy_phase,
                 rock_x, rock_y, rock_z, rock_yaw)
  ENEMY_STARTS.each_with_index do |start, index|
    enemy_x[index] = start[0]
    enemy_y[index] = start[1]
    enemy_z[index] = start[2]
    enemy_phase[index] = start[3]
  end
  ROCK_STARTS.each_with_index do |start, index|
    rock_x[index] = start[0]
    rock_y[index] = start[1]
    rock_z[index] = start[2]
    rock_yaw[index] = start[6]
  end
end

def player_nose_position(camera_x, camera_y, yaw, pitch, roll)
  x = 0.0
  y = 0.16 * PLAYER_SCALE
  z = 1.55 * PLAYER_SCALE
  yaw_sine = Math.sin(yaw)
  yaw_cosine = Math.cos(yaw)
  pitch_sine = Math.sin(pitch)
  pitch_cosine = Math.cos(pitch)
  roll_sine = Math.sin(roll)
  roll_cosine = Math.cos(roll)
  rotated_x = x * yaw_cosine - z * yaw_sine
  rotated_z = x * yaw_sine + z * yaw_cosine
  rotated_y = y * pitch_cosine - rotated_z * pitch_sine
  rotated_z = y * pitch_sine + rotated_z * pitch_cosine
  rolled_x = rotated_x * roll_cosine - rotated_y * roll_sine
  rolled_y = rotated_x * roll_sine + rotated_y * roll_cosine
  [camera_x + rolled_x, camera_y + PLAYER_CENTER_Y_OFFSET + rolled_y,
   PLAYER_CENTER_Z + rotated_z]
end

def reset_rings(ring_z)
  RING_START_Z.each_with_index do |start_z, index|
    ring_z[index] = start_z
  end
end

def clear_shots(shots_active, shots_z)
  SHOT_COUNT.times do |index|
    shots_active[index] = false
    shots_z[index] = 100.0
  end
end

def reset_rock_health(rock_health)
  ROCK_COUNT.times do |index|
    rock_health[index] = ROCK_HITS
  end
end

Game.init
unless Game::Runtime.capability?(Game::Capabilities::THREE_D)
  Game::Display.clear(0xf800)
  Game::Display.draw_text(16, 16, '3D NOT SUPPORTED', 0xffff)
  Game::Display.present
  loop { Game.tick(2) }
end

Game::ThreeD.set_viewport(0, 0, 320, 240)
Game::ThreeD.set_resolution(160, 120)

enemy_x = Array.new(ENEMY_COUNT, 0.0)
enemy_y = Array.new(ENEMY_COUNT, 0.0)
enemy_z = Array.new(ENEMY_COUNT, 0.0)
enemy_phase = Array.new(ENEMY_COUNT, 0.0)
rock_x = Array.new(ROCK_COUNT, 0.0)
rock_y = Array.new(ROCK_COUNT, 0.0)
rock_z = Array.new(ROCK_COUNT, 0.0)
rock_yaw = Array.new(ROCK_COUNT, 0.0)
rock_health = Array.new(ROCK_COUNT, ROCK_HITS)
reset_flight(enemy_x, enemy_y, enemy_z, enemy_phase,
             rock_x, rock_y, rock_z, rock_yaw)

star_x = Array.new(STAR_COUNT, 0.0)
star_y = Array.new(STAR_COUNT, 0.0)
star_z = Array.new(STAR_COUNT, 0.0)
STAR_COUNT.times do |index|
  star_x[index] = ((index * 37) % 100 - 50) / 8.0
  star_y[index] = ((index * 61) % 70 - 35) / 8.0
  star_z[index] = 3.0 + ((index * 29) % 330) / 10.0
end

ring_z = Array.new(RING_START_Z.length, 0.0)
reset_rings(ring_z)
star_vertices = Array.new(STAR_COUNT * 3, 0.0)
star_edges = Array.new(STAR_COUNT * 2, 0)
STAR_COUNT.times do |index|
  edge_index = index * 2
  star_edges[edge_index] = index
  star_edges[edge_index + 1] = index
end
tunnel_vertices = Array.new(RING_START_Z.length * BOX_VERTICES.length * 3, 0.0)
tunnel_edges = Array.new(RING_START_Z.length * BOX_EDGES.length, 0)
build_repeated_edges(tunnel_edges, BOX_EDGES, BOX_VERTICES.length,
                     RING_START_Z.length)
rock_vertices = Array.new(ROCK_COUNT * BOX_VERTICES.length * 3, 0.0)
rock_indices = Array.new(ROCK_COUNT * ROCK_TRIANGLES.length, 0)
rock_colors = Array.new(ROCK_COUNT * (ROCK_TRIANGLES.length / 3), 0)
ROCK_COUNT.times do |index|
  vertex_base = index * BOX_VERTICES.length
  triangle_base = index * ROCK_TRIANGLES.length
  ROCK_TRIANGLES.each_with_index do |vertex_index, triangle_index|
    rock_indices[triangle_base + triangle_index] = vertex_base + vertex_index
  end
  color_base = index * (ROCK_TRIANGLES.length / 3)
  ROCK_FACE_COLORS.each_with_index do |color, face_index|
    rock_colors[color_base + face_index * 2] = color
    rock_colors[color_base + face_index * 2 + 1] = color
  end
end
fighter_vertices = Array.new(ENEMY_COUNT * FIGHTER_VERTICES.length * 3, 0.0)
fighter_edges = Array.new(ENEMY_COUNT * FIGHTER_EDGES.length, 0)
build_repeated_edges(fighter_edges, FIGHTER_EDGES, FIGHTER_VERTICES.length,
                     ENEMY_COUNT)
cyan_vertices = Array.new(
  (SHOT_COUNT * SHOT_VERTICES.length + PLAYER_VERTICES.length) * 3, 0.0
)
cyan_edges = Array.new(SHOT_COUNT * SHOT_EDGES.length + PLAYER_EDGES.length, 0)
build_repeated_edges(cyan_edges, SHOT_EDGES, SHOT_VERTICES.length, SHOT_COUNT)
build_repeated_edges(
  cyan_edges, PLAYER_EDGES, PLAYER_VERTICES.length, 1,
  SHOT_COUNT * SHOT_VERTICES.length, SHOT_COUNT * SHOT_EDGES.length
)

camera_x = 0.0
camera_y = 0.0
camera_needs_update = true
shots_active = Array.new(SHOT_COUNT, false)
shots_x = Array.new(SHOT_COUNT, 0.0)
shots_y = Array.new(SHOT_COUNT, 0.0)
shots_z = Array.new(SHOT_COUNT, 100.0)
shot_cursor = 0
shot_cooldown_ms = 0
score = 0
shield = 3
damage_timer = 0
game_over = false
player_roll = 0.0
player_pitch = 0.0
show_hud = true
frame = 0
fps_started_ms = Game::Time.millis
previous_frame_ms = fps_started_ms
fps_frames = 0
measured_fps = 0

loop do
  frame += 1
  fps_frames += 1
  now_ms = Game::Time.millis
  frame_delta_ms = now_ms - previous_frame_ms
  previous_frame_ms = now_ms
  frame_delta_ms = 1 if frame_delta_ms < 1
  frame_delta_ms = MAX_FRAME_DELTA_MS if frame_delta_ms > MAX_FRAME_DELTA_MS
  elapsed_ms = now_ms - fps_started_ms
  if elapsed_ms >= 1000
    measured_fps = fps_frames * 1000 / elapsed_ms
    fps_frames = 0
    fps_started_ms = now_ms
  end

  if Game::Input.just_pressed?(Game::Input::START)
    camera_x = 0.0
    camera_y = 0.0
    camera_needs_update = true
    clear_shots(shots_active, shots_z)
    shot_cursor = 0
    shot_cooldown_ms = 0
    score = 0
    shield = 3
    damage_timer = 0
    game_over = false
    player_roll = 0.0
    player_pitch = 0.0
    reset_rings(ring_z)
    reset_flight(enemy_x, enemy_y, enemy_z, enemy_phase,
                 rock_x, rock_y, rock_z, rock_yaw)
    reset_rock_health(rock_health)
  end
  show_hud = !show_hud if Game::Input.just_pressed?(Game::Input::SELECT)

  move_x = 0
  move_y = 0
  unless game_over
    move_x -= 1 if Game::Input.pressed?(Game::Input::LEFT)
    move_x += 1 if Game::Input.pressed?(Game::Input::RIGHT)
    move_y += 1 if Game::Input.pressed?(Game::Input::UP)
    move_y -= 1 if Game::Input.pressed?(Game::Input::DOWN)
  end
  camera_x += move_x * 0.10
  camera_y += move_y * 0.08
  camera_x = -4.5 if camera_x < -4.5
  camera_x = 4.5 if camera_x > 4.5
  camera_y = -3.0 if camera_y < -3.0
  camera_y = 3.0 if camera_y > 3.0
  camera_needs_update = true if move_x != 0 || move_y != 0

  # Roll into the turn. The negative sign matches the screen-space direction
  # of the paper-airplane model, so right input banks visibly to the right.
  target_player_roll = move_x * -0.24
  target_player_pitch = move_y * -0.16
  target_player_roll = 0.0 if game_over
  target_player_pitch = 0.0 if game_over
  player_roll += (target_player_roll - player_roll) * 0.30
  player_pitch += (target_player_pitch - player_pitch) * 0.30
  player_yaw = Math.sin(frame * 0.04) * 0.025
  player_hit_x = camera_x
  player_hit_y = camera_y + PLAYER_CENTER_Y_OFFSET

  boost = !game_over && Game::Input.pressed?(Game::Input::B)
  # Keep normal flight at the former BOOST pace, while leaving BOOST useful
  # as a faster risk/reward mode.
  flight_speed = boost ? 0.36 : 0.24
  shot_cooldown_ms -= frame_delta_ms if shot_cooldown_ms > 0
  if !game_over && Game::Input.pressed?(Game::Input::A) && shot_cooldown_ms <= 0
    player_nose_x, player_nose_y, player_nose_z = player_nose_position(
      camera_x, camera_y, player_yaw, player_pitch, player_roll
    )
    shots_active[shot_cursor] = true
    shots_x[shot_cursor] = player_nose_x
    shots_y[shot_cursor] = player_nose_y
    shots_z[shot_cursor] = player_nose_z
    shot_cursor += 1
    shot_cursor = 0 if shot_cursor >= SHOT_COUNT
    shot_cooldown_ms = SHOT_COOLDOWN_MS
  end

  unless game_over
    STAR_COUNT.times do |index|
      star_z[index] -= flight_speed * 1.2
      star_z[index] = 36.0 if star_z[index] < 1.0
    end
    RING_START_Z.length.times do |index|
      ring_z[index] -= flight_speed * 1.35
      ring_z[index] += RING_LOOP_LENGTH if ring_z[index] < 4.0
    end
    ENEMY_COUNT.times do |index|
      enemy_z[index] -= flight_speed
      if enemy_z[index] < 2.5
        enemy_z[index] = 40.0 + index * 3.0
        enemy_x[index] = ENEMY_STARTS[index][0]
        enemy_y[index] = ENEMY_STARTS[index][1]
      end
    end
    ROCK_COUNT.times do |index|
      rock_z[index] -= flight_speed * 0.85
      if rock_z[index] < 3.0
        rock_z[index] = 38.0 + index * 13.0
        rock_health[index] = ROCK_HITS
      end
      rock_yaw[index] += 0.012
    end
  end

  shot_speed = boost ? SHOT_SPEED_PER_SECOND * 1.4 : SHOT_SPEED_PER_SECOND
  if !game_over
    SHOT_COUNT.times do |shot_index|
      if shots_active[shot_index]
        shots_z[shot_index] += shot_speed * frame_delta_ms / 1000.0
        if shots_z[shot_index] > 70.0
          shots_active[shot_index] = false
        else
          ENEMY_COUNT.times do |enemy_index|
            dx = enemy_x[enemy_index] - shots_x[shot_index]
            dy = enemy_y[enemy_index] - shots_y[shot_index]
            dz = enemy_z[enemy_index] - shots_z[shot_index]
            if dx * dx + dy * dy < 0.75 && dz.abs < 1.1
              score += 100
              enemy_z[enemy_index] = 40.0 + enemy_index * 3.0
              enemy_x[enemy_index] = ENEMY_STARTS[enemy_index][0]
              enemy_y[enemy_index] = ENEMY_STARTS[enemy_index][1]
              shots_active[shot_index] = false
              break
            end
          end
          if shots_active[shot_index]
            ROCK_COUNT.times do |rock_index|
              dx = rock_x[rock_index] - shots_x[shot_index]
              dy = rock_y[rock_index] - shots_y[shot_index]
              dz = rock_z[rock_index] - shots_z[shot_index]
              if dx * dx + dy * dy < 1.35 && dz.abs < 1.3
                rock_health[rock_index] -= 1
                score += 25
                if rock_health[rock_index] <= 0
                  score += 75
                  rock_z[rock_index] = 38.0 + rock_index * 13.0
                  rock_health[rock_index] = ROCK_HITS
                end
                shots_active[shot_index] = false
                break
              end
            end
          end
        end
      end
    end
  end

  if !game_over
    ROCK_COUNT.times do |index|
      if rock_z[index] < 3.4
        dx = rock_x[index] - player_hit_x
        dy = rock_y[index] - player_hit_y
        if dx * dx + dy * dy < 2.0
          shield -= 1
          damage_timer = 30
          rock_z[index] = 38.0 + index * 13.0
          rock_health[index] = ROCK_HITS
        end
      end
    end
    ENEMY_COUNT.times do |index|
      if enemy_z[index] < 2.9
        dx = enemy_x[index] - player_hit_x
        dy = enemy_y[index] - player_hit_y
        if dx * dx + dy * dy < 0.9
          shield -= 1
          damage_timer = 30
          enemy_z[index] = 40.0 + index * 3.0
        end
      end
    end
    game_over = true if shield <= 0
  end
  damage_timer -= 1 if damage_timer > 0

  star_vertices_offset = 0
  STAR_COUNT.times do |index|
    star_vertices[star_vertices_offset] = star_x[index]
    star_vertices[star_vertices_offset + 1] = star_y[index]
    star_vertices[star_vertices_offset + 2] = star_z[index]
    star_vertices_offset += 3
  end

  tunnel_vertex_offset = 0
  ring_z.each do |current_ring_z|
    tunnel_vertex_offset = append_model_vertices(
      tunnel_vertices, tunnel_vertex_offset, BOX_VERTICES,
      6.0, 4.0, 0.08, 0.0, 0.0, 0.0, 0.0, current_ring_z
    )
  end

  rock_vertex_offset = 0
  ROCK_COUNT.times do |index|
    start = ROCK_STARTS[index]
    rock_vertex_offset = append_transformed_vertices(
      rock_vertices, rock_vertex_offset, BOX_VERTICES,
      start[3], start[4], start[5], rock_yaw[index],
      Math.sin(frame * 0.013 + index) * 0.18,
      rock_x[index], rock_y[index], rock_z[index]
    )
  end

  fighter_vertex_offset = 0
  ENEMY_COUNT.times do |index|
    # Enemies face the player while closing in; the player model keeps its
    # nose pointed into +Z, so enemies need a 180-degree yaw flip.
    yaw = Math::PI + Math.sin(frame * 0.035 + enemy_phase[index]) * 0.35
    pitch = Math.sin(frame * 0.022 + enemy_phase[index]) * 0.18
    scale = 0.62 + (index % 3) * 0.08
    fighter_vertex_offset = append_model_vertices(
      fighter_vertices, fighter_vertex_offset, FIGHTER_VERTICES,
      scale, scale, scale, yaw, pitch,
      enemy_x[index], enemy_y[index], enemy_z[index]
    )
  end
  cyan_vertex_offset = 0
  SHOT_COUNT.times do |shot_index|
    shot_z = shots_active[shot_index] ? shots_z[shot_index] : 100.0
    cyan_vertex_offset = append_model_vertices(
      cyan_vertices, cyan_vertex_offset, SHOT_VERTICES,
      1.0, 1.0, 1.0, 0.0, 0.0,
      shots_x[shot_index], shots_y[shot_index], shot_z
    )
  end

  append_model_vertices(
    cyan_vertices, cyan_vertex_offset, PLAYER_VERTICES,
    PLAYER_SCALE, PLAYER_SCALE, PLAYER_SCALE, player_yaw, player_pitch,
    camera_x, camera_y + PLAYER_CENTER_Y_OFFSET, PLAYER_CENTER_Z, player_roll
  )

  Game::Display.begin_frame
  Game::Display.clear(0x0000)
  if camera_needs_update
    Game::ThreeD.set_camera(camera_x, camera_y, 0.0, 0.0, 0.0, 70, 0.1, 80)
    camera_needs_update = false
  end
  Game::ThreeD.begin_frame
  Game::ThreeD.draw_wireframe(star_vertices, star_edges, 0x7bef, true)
  Game::ThreeD.draw_wireframe(tunnel_vertices, tunnel_edges, 0x03e0, true)
  Game::ThreeD.draw_indexed_mesh(rock_vertices, rock_indices, rock_colors)
  Game::ThreeD.draw_wireframe(fighter_vertices, fighter_edges, 0xff20, true)
  Game::ThreeD.draw_wireframe(cyan_vertices, cyan_edges, 0x07ff, true)

  # The reticle is 2D cockpit feedback; the projectile itself is the cyan 3D
  # wireframe mesh drawn above.
  Game::Display.draw_line(148, 120, 156, 120, RETICLE_THICKNESS, 0xffff)
  Game::Display.draw_line(164, 120, 172, 120, RETICLE_THICKNESS, 0xffff)
  Game::Display.draw_line(160, 112, 160, 116, RETICLE_THICKNESS, 0xffff)
  Game::Display.draw_line(160, 124, 160, 128, RETICLE_THICKNESS, 0xffff)
  shot_visible = false
  SHOT_COUNT.times do |shot_index|
    if shots_active[shot_index]
      shot_visible = true
    end
  end

  if show_hud
    Game::Display.draw_text(8, 8, 'WIRE FRAME FLIGHT', 0xffff)
    Game::Display.draw_text(8, 24, "SCORE #{score}  SH #{shield}", 0xffff)
    Game::Display.draw_text(8, 40, "FPS #{measured_fps}", 0xffff)
  else
    Game::Display.draw_text(8, 8, "FPS #{measured_fps}", 0xffff)
  end
  Game::Display.draw_text(248, 8, 'BOOST', 0xffe0) if boost
  Game::Display.draw_text(248, 24, 'SHOT', 0xffff) if shot_visible
  Game::Display.draw_text(240, 40, 'DAMAGE', 0xf800) if damage_timer > 0
  if game_over
    Game::Display.draw_text(116, 112, 'GAME OVER', 0xf800)
    Game::Display.draw_text(104, 128, 'PRESS START', 0xffff)
  end
  Game::Display.present
  Game.tick(60)
end
