/* Pixel data and sprite-sheet encoding, shared by the editor and host tests. */
(function (root) {
  'use strict';

  class PixelArtModel {
    static MAX_SIZE = 64;
    static MAX_FRAMES = 32;

    constructor(data = {}) {
      this.width = data.width ?? 16;
      this.height = data.height ?? 16;
      this.checkSize(this.width, this.height);
      const frames = data.frames ?? [new Uint32Array(this.width * this.height)];
      if (!Array.isArray(frames) || frames.length < 1 || frames.length > PixelArtModel.MAX_FRAMES) {
        throw new Error('フレーム数は1〜32で指定してください');
      }
      this.frames = frames.map((frame) => {
        if (frame.length !== this.width * this.height) throw new Error('画像のサイズを確認してください');
        return Uint32Array.from(frame);
      });
      this.selected = Number.isInteger(data.selected) ? Math.min(Math.max(0, data.selected), this.frames.length - 1) : 0;
      this.name = typeof data.name === 'string' ? data.name : 'sprite';
      this.fps = Number.isInteger(data.fps) && data.fps >= 1 && data.fps <= 24 ? data.fps : 8;
      this.undoStack = [];
      this.redoStack = [];
      this.stroke = null;
    }

    checkSize(width, height) {
      if (![width, height].every((n) => Number.isInteger(n) && n >= 1 && n <= PixelArtModel.MAX_SIZE)) {
        throw new Error('幅と高さは1〜64で指定してください');
      }
    }

    snapshot() {
      return { width: this.width, height: this.height, selected: this.selected,
        frames: this.frames.map((frame) => frame.slice()) };
    }

    serialize() { return { version: 1, ...this.snapshot(), name: this.name, fps: this.fps }; }

    remember(before) {
      this.undoStack.push(before);
      this.redoStack = [];
      let bytes = this.undoStack.reduce((sum, item) => sum + item.width * item.height * item.frames.length * 4, 0);
      while (this.undoStack.length > 30 || bytes > 8 * 1024 * 1024) {
        const removed = this.undoStack.shift();
        bytes -= removed.width * removed.height * removed.frames.length * 4;
      }
    }

    beginStroke() {
      this.endStroke();
      this.stroke = { before: this.snapshot(), changed: false };
    }

    endStroke() {
      if (!this.stroke) return false;
      const changed = this.stroke.changed;
      if (changed) this.remember(this.stroke.before);
      this.stroke = null;
      return changed;
    }

    pixel(x, y) { return this.frames[this.selected][y * this.width + x] ?? 0; }

    paint(x, y, color) {
      if (x < 0 || y < 0 || x >= this.width || y >= this.height) return;
      const frame = this.frames[this.selected];
      const index = y * this.width + x;
      if (frame[index] === color) return;
      frame[index] = color;
      this.stroke.changed = true;
    }

    line(x0, y0, x1, y1, color) {
      if (!this.stroke) this.beginStroke();
      const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0);
      const sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
      let error = dx + dy;
      while (true) {
        this.paint(x0, y0, color);
        if (x0 === x1 && y0 === y1) break;
        const twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
      }
    }

    fill(x, y, color) {
      if (x < 0 || y < 0 || x >= this.width || y >= this.height) return false;
      this.endStroke();
      const target = this.pixel(x, y);
      if (target === color) return false;
      this.beginStroke();
      const queue = [[x, y]];
      this.paint(x, y, color);
      while (queue.length) {
        const [px, py] = queue.pop();
        for (const [nx, ny] of [[px - 1, py], [px + 1, py], [px, py - 1], [px, py + 1]]) {
          if (nx >= 0 && ny >= 0 && nx < this.width && ny < this.height && this.pixel(nx, ny) === target) {
            this.paint(nx, ny, color);
            queue.push([nx, ny]);
          }
        }
      }
      return this.endStroke();
    }

    addFrame(copy = false) {
      this.endStroke();
      if (this.frames.length >= PixelArtModel.MAX_FRAMES) return false;
      const before = this.snapshot();
      const frame = copy ? this.frames[this.selected].slice() : new Uint32Array(this.width * this.height);
      this.frames.splice(++this.selected, 0, frame);
      this.remember(before);
      return true;
    }

    deleteFrame() {
      this.endStroke();
      if (this.frames.length === 1) return false;
      const before = this.snapshot();
      this.frames.splice(this.selected, 1);
      this.selected = Math.min(this.selected, this.frames.length - 1);
      this.remember(before);
      return true;
    }

    moveFrame(direction) {
      this.endStroke();
      const next = this.selected + direction;
      if (next < 0 || next >= this.frames.length) return false;
      const before = this.snapshot();
      [this.frames[this.selected], this.frames[next]] = [this.frames[next], this.frames[this.selected]];
      this.selected = next;
      this.remember(before);
      return true;
    }

    resize(width, height) {
      this.checkSize(width, height);
      this.endStroke();
      if (width === this.width && height === this.height) return false;
      const before = this.snapshot();
      this.frames = this.frames.map((frame) => {
        const resized = new Uint32Array(width * height);
        for (let y = 0; y < Math.min(height, this.height); y++) {
          for (let x = 0; x < Math.min(width, this.width); x++) resized[y * width + x] = frame[y * this.width + x];
        }
        return resized;
      });
      this.width = width;
      this.height = height;
      this.remember(before);
      return true;
    }

    undo() {
      this.endStroke();
      if (!this.undoStack.length) return false;
      this.redoStack.push(this.snapshot());
      Object.assign(this, this.undoStack.pop());
      return true;
    }

    redo() {
      if (!this.redoStack.length) return false;
      this.undoStack.push(this.snapshot());
      Object.assign(this, this.redoStack.pop());
      return true;
    }

    importSheet(rgba, width, height) {
      this.endStroke();
      let frameWidth = this.width;
      if (height !== this.height || width % frameWidth !== 0) {
        this.checkSize(width, height);
        frameWidth = width;
      }
      this.checkSize(frameWidth, height);
      const count = width / frameWidth;
      if (count < 1 || count > PixelArtModel.MAX_FRAMES || rgba.length !== width * height * 4) {
        throw new Error('1コマの幅・高さと画像を確認してください');
      }
      const before = this.snapshot();
      const frames = Array.from({ length: count }, () => new Uint32Array(frameWidth * height));
      for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
          const offset = (y * width + x) * 4;
          if (rgba[offset + 3] >= 128) {
            frames[Math.floor(x / frameWidth)][y * frameWidth + x % frameWidth] =
              ((rgba[offset] << 24) | (rgba[offset + 1] << 16) | (rgba[offset + 2] << 8) | 255) >>> 0;
          }
        }
      }
      this.width = frameWidth;
      this.height = height;
      this.frames = frames;
      this.selected = 0;
      this.remember(before);
    }

    rgbaSheet() {
      const width = this.width * this.frames.length;
      const bytes = new Uint8ClampedArray(width * this.height * 4);
      this.frames.forEach((frame, index) => {
        frame.forEach((color, pixel) => {
          const offset = (Math.floor(pixel / this.width) * width + index * this.width + pixel % this.width) * 4;
          bytes[offset] = color >>> 24;
          bytes[offset + 1] = (color >>> 16) & 255;
          bytes[offset + 2] = (color >>> 8) & 255;
          bytes[offset + 3] = color & 255;
        });
      });
      return { width, height: this.height, bytes };
    }

    rawSheet() {
      const { bytes: rgba } = this.rgbaSheet();
      const bytes = new Uint8Array(rgba.length / 2);
      for (let i = 0, out = 0; i < rgba.length; i += 4, out += 2) {
        let color = 0xf81f;
        if (rgba[i + 3] >= 128) {
          color = (Math.floor((rgba[i] * 31 + 127) / 255) << 11) |
            (Math.floor((rgba[i + 1] * 63 + 127) / 255) << 5) | Math.floor((rgba[i + 2] * 31 + 127) / 255);
          // Preserve opaque magenta instead of turning it into the transparency key.
          if (color === 0xf81f) color = 0xf81e;
        }
        bytes[out] = color >>> 8;
        bytes[out + 1] = color & 255;
      }
      return bytes;
    }
  }

  if (typeof module !== 'undefined' && module.exports) module.exports = PixelArtModel;
  else root.PixelArtModel = PixelArtModel;
})(typeof window !== 'undefined' ? window : globalThis);
