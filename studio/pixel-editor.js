(function () {
  'use strict';
  const t = (ja, en) => window.StudioI18n.t(ja, en);
  const modelErrors = {
    'フレーム数は1〜32で指定してください': 'Choose between 1 and 32 frames',
    '画像のサイズを確認してください': 'Check the image dimensions',
    '幅と高さは1〜64で指定してください': 'Width and height must be between 1 and 64',
    '1コマの幅・高さと画像を確認してください': 'Check the frame dimensions and image'
  };

  window.PixelArtEditor = class PixelArtEditor {
    constructor(dialog, { onChange, onApply }) {
      this.dialog = dialog;
      this.onChange = onChange;
      this.onApply = onApply;
      this.model = new PixelArtModel();
      this.sourceAssetName = null;
      this.tool = 'pencil';
      this.color = '#cd654e';
      this.cursor = [0, 0];
      this.pointer = null;
      this.playing = false;
      this.animation = null;
      this.status = '';
      this.statusError = false;
      this.publishTimer = null;
      this.$ = (id) => dialog.querySelector(`#pixel-${id}`);
      this.canvas = this.$('canvas');
      this.preview = this.$('preview');
      this.installEvents();
      this.load(null);
    }

    rawName() { return this.model.name.trim().replace(/\.(png|raw)$/i, '') + '.raw'; }
    validName() { return /^[A-Za-z0-9][A-Za-z0-9._-]*\.raw$/.test(this.rawName()) && !this.rawName().includes('..'); }
    serialize() { return { ...this.model.serialize(), sourceAssetName: this.sourceAssetName }; }

    load(data) {
      this.stop();
      this.finishStroke();
      try {
        this.model = new PixelArtModel(data || {});
        this.sourceAssetName = data?.sourceAssetName || null;
        this.message('');
      } catch (error) {
        this.model = new PixelArtModel();
        this.sourceAssetName = null;
        this.message(error.message, true);
      }
      this.cursor = [0, 0];
      this.$('name').value = this.model.name;
      this.$('fps').value = this.model.fps;
      this.render();
    }

    open(data) {
      if (data) this.load(data);
      if (!this.dialog.open) this.dialog.showModal();
      this.render();
      this.canvas.focus();
    }

    message(text, error = false) {
      this.status = window.StudioI18n.language === 'en' ? (modelErrors[text] || text) : text;
      this.statusError = error;
      this.publish();
    }

    changed() {
      this.message('');
      this.render();
      this.onChange();
    }

    act(action) {
      this.stop();
      this.finishStroke();
      if (action()) this.changed();
    }

    setTool(tool) {
      this.tool = tool;
      this.publish();
    }

    setColor(color) {
      this.color = color;
      this.$('color').value = color;
      this.publish();
    }

    publish() {
      clearTimeout(this.publishTimer);
      this.publishTimer = setTimeout(() => window.GamingWeb.emit('pixel-state', {
        width: this.model.width, height: this.model.height, count: this.model.frames.length,
        selected: this.model.selected, tool: this.tool, color: this.color,
        undo: !!this.model.undoStack.length, redo: !!this.model.redoStack.length,
        playing: this.playing, name: this.validName() ? this.rawName() : 'sprite.raw', fps: this.model.fps,
        apply: this.rawName() === this.sourceAssetName ? t('素材を更新', 'Update asset') : t('素材に追加', 'Add to assets'),
        message: this.status, error: this.statusError
      }), 0);
    }

    close() { this.dialog.close(); }
    rename() {
      this.model.name = this.$('name').value;
      this.publish();
      this.onChange();
    }
    changeFps() {
      if (!this.$('fps').reportValidity()) return;
      this.model.fps = this.$('fps').valueAsNumber;
      this.publish();
      this.onChange();
    }
    togglePlay() { this.playing ? this.stop() : this.play(); }
    selectFrame(index) {
      this.stop(); this.finishStroke(); this.model.selected = index; this.changed();
    }
    action(name) {
      this.act(() => {
        switch (name) {
          case 'undo': return this.model.undo();
          case 'redo': return this.model.redo();
          case 'add': return this.model.addFrame();
          case 'duplicate': return this.model.addFrame(true);
          case 'delete': return this.model.deleteFrame();
          case 'left': return this.model.moveFrame(-1);
          case 'right': return this.model.moveFrame(1);
          case 'resize': return this.model.resize(this.$('width').valueAsNumber, this.$('height').valueAsNumber);
        }
        return false;
      });
    }

    rgbaColor() { return (parseInt(this.color.slice(1), 16) * 256 + 255) >>> 0; }

    installEvents() {
      this.dialog.addEventListener('close', () => { this.stop(); this.finishStroke(); });
      this.canvas.addEventListener('contextmenu', (event) => event.preventDefault());
      this.canvas.addEventListener('pointerdown', (event) => {
        if (this.pointer || (event.button !== 0 && event.button !== 2)) return;
        event.preventDefault();
        this.stop();
        this.canvas.focus();
        const [x, y] = this.point(event);
        this.cursor = [x, y];
        if (this.tool === 'picker') { this.pick(x, y); return; }
        const color = this.tool === 'eraser' || event.button === 2 ? 0 : this.rgbaColor();
        if (this.tool === 'fill') { this.act(() => this.model.fill(x, y, color)); return; }
        this.canvas.setPointerCapture(event.pointerId);
        this.model.beginStroke();
        this.pointer = { id: event.pointerId, x, y, color };
        this.model.line(x, y, x, y, color);
        this.renderDrawing();
        this.renderPreview(this.model.selected);
      });
      this.canvas.addEventListener('pointermove', (event) => {
        if (!this.pointer || this.pointer.id !== event.pointerId) return;
        const [x, y] = this.point(event);
        this.model.line(this.pointer.x, this.pointer.y, x, y, this.pointer.color);
        Object.assign(this.pointer, { x, y });
        this.cursor = [x, y];
        this.renderDrawing();
        this.renderPreview(this.model.selected);
      });
      for (const event of ['pointerup', 'pointercancel', 'lostpointercapture']) {
        this.canvas.addEventListener(event, () => this.finishStroke());
      }
      this.canvas.addEventListener('focus', () => this.renderDrawing());
      this.canvas.addEventListener('blur', () => this.renderDrawing());
      this.dialog.addEventListener('keydown', (event) => this.keydown(event));
    }

    point(event) {
      const rect = this.canvas.getBoundingClientRect();
      return [Math.max(0, Math.min(this.model.width - 1, Math.floor((event.clientX - rect.left) * this.model.width / rect.width))),
        Math.max(0, Math.min(this.model.height - 1, Math.floor((event.clientY - rect.top) * this.model.height / rect.height)))];
    }

    finishStroke() {
      this.pointer = null;
      if (this.model.endStroke()) this.changed();
    }

    pick(x, y) {
      const color = this.model.pixel(x, y);
      if (color) this.setColor('#' + (color >>> 8).toString(16).padStart(6, '0'));
      this.setTool('pencil');
    }

    keydown(event) {
      if (event.target.matches('input, select, textarea')) return;
      const key = event.key.toLowerCase();
      if ((event.metaKey || event.ctrlKey) && key === 'z') {
        event.preventDefault();
        this.act(() => event.shiftKey ? this.model.redo() : this.model.undo());
      } else if (!event.metaKey && !event.ctrlKey && { b: 'pencil', e: 'eraser', f: 'fill', i: 'picker' }[key]) {
        this.setTool({ b: 'pencil', e: 'eraser', f: 'fill', i: 'picker' }[key]);
      } else if (event.target === this.canvas) {
        const moves = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };
        if (moves[event.key]) {
          event.preventDefault();
          this.stop();
          const [dx, dy] = moves[event.key];
          this.cursor = [Math.max(0, Math.min(this.model.width - 1, this.cursor[0] + dx)),
            Math.max(0, Math.min(this.model.height - 1, this.cursor[1] + dy))];
          this.renderDrawing();
        } else if (event.key === ' ' || event.key === 'Enter') {
          event.preventDefault();
          const [x, y] = this.cursor;
          if (this.tool === 'picker') { this.pick(x, y); return; }
          const color = this.tool === 'eraser' ? 0 : this.rgbaColor();
          this.act(() => {
            if (this.tool === 'fill') return this.model.fill(x, y, color);
            this.model.beginStroke();
            this.model.line(x, y, x, y, color);
            return this.model.endStroke();
          });
        }
      }
    }

    drawFrame(canvas, frame, edge, onion = null, grid = false) {
      const { width, height } = this.model;
      const scale = Math.max(1, Math.floor(edge / Math.max(width, height)));
      canvas.width = width * scale;
      canvas.height = height * scale;
      const context = canvas.getContext('2d');
      for (let y = 0; y < canvas.height; y += 8) {
        for (let x = 0; x < canvas.width; x += 8) {
          context.fillStyle = ((x / 8 + y / 8) % 2) ? '#e8dfcb' : '#fffaf0';
          context.fillRect(x, y, 8, 8);
        }
      }
      const paint = (pixels, alpha) => {
        context.globalAlpha = alpha;
        pixels.forEach((color, i) => {
          if (!color) return;
          context.fillStyle = '#' + (color >>> 8).toString(16).padStart(6, '0');
          context.fillRect((i % width) * scale, Math.floor(i / width) * scale, scale, scale);
        });
        context.globalAlpha = 1;
      };
      if (onion) paint(onion, 0.25);
      paint(frame, 1);
      if (grid && scale >= 4) {
        context.strokeStyle = '#493b3238';
        context.beginPath();
        for (let x = 1; x < width; x++) { context.moveTo(x * scale + 0.5, 0); context.lineTo(x * scale + 0.5, canvas.height); }
        for (let y = 1; y < height; y++) { context.moveTo(0, y * scale + 0.5); context.lineTo(canvas.width, y * scale + 0.5); }
        context.stroke();
      }
      return { context, scale };
    }

    renderDrawing() {
      const { frames, selected } = this.model;
      const onion = this.$('onion').checked && frames.length > 1 ? frames[(selected + frames.length - 1) % frames.length] : null;
      const { context, scale } = this.drawFrame(this.canvas, frames[selected], 384, onion, this.$('grid').checked);
      if (document.activeElement === this.canvas) {
        context.strokeStyle = '#493b32';
        context.lineWidth = 2;
        context.strokeRect(this.cursor[0] * scale + 1, this.cursor[1] * scale + 1, scale - 2, scale - 2);
      }
    }

    renderPreview(index) {
      this.drawFrame(this.preview, this.model.frames[index], 128);
      this.$('position').textContent = `${index + 1} / ${this.model.frames.length}`;
    }

    render() {
      this.cursor = [Math.min(this.cursor[0], this.model.width - 1), Math.min(this.cursor[1], this.model.height - 1)];
      this.$('width').value = this.model.width;
      this.$('height').value = this.model.height;
      this.publish();
      this.renderCanvases();
    }

    renderCanvases() {
      this.$('frames').querySelectorAll('[data-frame]').forEach((canvas) => {
        const frame = this.model.frames[Number(canvas.dataset.frame)];
        if (frame) this.drawFrame(canvas, frame, 48);
      });
      this.renderDrawing();
      if (!this.playing) this.renderPreview(this.model.selected);
    }

    play() {
      if (!this.$('fps').reportValidity()) return;
      this.finishStroke();
      this.model.fps = this.$('fps').valueAsNumber;
      this.playing = true;
      this.publish();
      let started = null;
      let shown = -1;
      const tick = (time) => {
        if (!this.playing) return;
        started ??= time;
        const index = Math.floor((time - started) * this.model.fps / 1000) % this.model.frames.length;
        if (index !== shown) { this.renderPreview(index); shown = index; }
        this.animation = requestAnimationFrame(tick);
      };
      this.animation = requestAnimationFrame(tick);
    }

    stop() {
      this.playing = false;
      if (this.animation !== null) cancelAnimationFrame(this.animation);
      this.animation = null;
      this.publish();
      if (this.model) this.renderPreview(this.model.selected);
    }

    apply() {
      this.finishStroke();
      if (!this.validName()) { this.message(t('名前は英数字・ハイフン・アンダースコア・ドットで指定してください', 'Use letters, numbers, hyphens, underscores, or dots'), true); return; }
      try {
        const name = this.rawName();
        this.onApply({ name, bytes: this.model.rawSheet(), pixelArt: this.serialize(), sourceAssetName: this.sourceAssetName });
        this.sourceAssetName = name;
        this.onChange();
        this.render();
        this.message(t(`${name} を保存しました`, `Saved ${name}`));
      } catch (error) { this.message(error.message, true); }
    }

    async importPng() {
      const file = this.$('import').files[0];
      this.$('import').value = '';
      if (!file) return;
      let image;
      try {
        image = await createImageBitmap(file);
        if (image.width > PixelArtModel.MAX_SIZE * PixelArtModel.MAX_FRAMES || image.height > PixelArtModel.MAX_SIZE) {
          throw new Error('1コマの幅・高さと画像を確認してください');
        }
        const canvas = document.createElement('canvas');
        canvas.width = image.width;
        canvas.height = image.height;
        const context = canvas.getContext('2d');
        context.drawImage(image, 0, 0);
        this.stop();
        this.finishStroke();
        this.model.importSheet(context.getImageData(0, 0, image.width, image.height).data, image.width, image.height);
        this.model.name = file.name.replace(/\.png$/i, '');
        this.$('name').value = this.model.name;
        this.sourceAssetName = null;
        this.cursor = [0, 0];
        this.changed();
      } catch (error) { this.message(error.message, true); }
      finally { if (image) image.close(); }
    }

    download() {
      this.finishStroke();
      if (!this.validName()) { this.message(t('名前は英数字・ハイフン・アンダースコア・ドットで指定してください', 'Use letters, numbers, hyphens, underscores, or dots'), true); return; }
      const { width, height, bytes } = this.model.rgbaSheet();
      const filename = this.rawName().replace(/\.raw$/, '.png');
      const canvas = document.createElement('canvas');
      canvas.width = width;
      canvas.height = height;
      canvas.getContext('2d').putImageData(new ImageData(bytes, width, height), 0, 0);
      canvas.toBlob((blob) => {
        if (!blob) { this.message(t('PNGの保存を再試行してください', 'Please try saving the PNG again'), true); return; }
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url;
        link.download = filename;
        link.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
        this.message(t('PNGを保存しました', 'PNG saved'));
      }, 'image/png');
    }
  };
})();
