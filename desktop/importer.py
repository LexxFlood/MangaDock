"""MangaDock importer. Python 3.11+, Pillow and PyMuPDF; optional rarfile."""
from pathlib import Path
import io, re, zipfile, tarfile, tempfile, os, argparse, warnings
from PIL import Image, ImageOps

IMAGES = {'.jpg', '.jpeg', '.png', '.webp', '.bmp', '.tif', '.tiff', '.gif', '.tga'}
LIMIT = 64 * 1024 * 1024
MAX_PAGES = 10000
Image.MAX_IMAGE_PIXELS = 40_000_000
warnings.simplefilter('error', Image.DecompressionBombWarning)

def natural(s):
    return tuple((1, int(x)) if x.isdigit() else (0, x.casefold()) for x in re.split(r'(\d+)', str(s)))

def visible(name):
    return not any(p.startswith('.') or p == '__MACOSX' for p in name.replace('\\', '/').split('/'))

def bounded_read(stream):
    data = stream.read(LIMIT + 1)
    if len(data) > LIMIT:
        raise ValueError('Página maior que 64 MiB. Reduza o arquivo antes de importar.')
    return data

def sources(path):
    """Yield (label, bytes), without extracting archive paths to disk."""
    ext = path.suffix.lower()
    if path.is_dir():
        names = sorted((p for p in path.rglob('*') if p.is_file() and p.suffix.lower() in IMAGES and visible(str(p.relative_to(path)))), key=natural)
        for p in names:
            with p.open('rb') as f: yield str(p), bounded_read(f)
    elif ext in IMAGES:
        with path.open('rb') as f: yield path.name, bounded_read(f)
    elif ext in {'.zip', '.cbz', '.cdz'}:
        with zipfile.ZipFile(path) as archive:
            for entry in sorted(archive.infolist(), key=lambda x: natural(x.filename)):
                if not entry.is_dir() and Path(entry.filename).suffix.lower() in IMAGES and visible(entry.filename):
                    if entry.flag_bits & 1: raise ValueError('Arquivo com senha não é suportado.')
                    if entry.file_size > LIMIT: raise ValueError('Página compactada excede 64 MiB.')
                    with archive.open(entry) as f: yield entry.filename, bounded_read(f)
    elif ext in {'.tar', '.cbt'}:
        with tarfile.open(path, 'r:*') as archive:
            for entry in sorted(archive.getmembers(), key=lambda x: natural(x.name)):
                if entry.isfile() and Path(entry.name).suffix.lower() in IMAGES and visible(entry.name):
                    if entry.size > LIMIT: raise ValueError('Página compactada excede 64 MiB.')
                    with archive.extractfile(entry) as f: yield entry.name, bounded_read(f)
    elif ext in {'.rar', '.cbr'}:
        try: import rarfile
        except ImportError: raise ValueError('Instale rarfile e UnRAR para importar CBR/RAR.') from None
        with rarfile.RarFile(path) as archive:
            if archive.needs_password(): raise ValueError('Arquivo com senha não é suportado.')
            for entry in sorted(archive.infolist(), key=lambda x: natural(x.filename)):
                if not entry.isdir() and Path(entry.filename).suffix.lower() in IMAGES and visible(entry.filename):
                    if entry.file_size > LIMIT: raise ValueError('Página compactada excede 64 MiB.')
                    with archive.open(entry) as f: yield entry.filename, bounded_read(f)
    else:
        raise ValueError('Formato não suportado: ' + ext)

def pages(path, width):
    if path.suffix.lower() == '.pdf':
        import pymupdf
        with pymupdf.open(path) as doc:
            if doc.needs_pass: raise ValueError('PDF com senha não é suportado.')
            for page in doc:
                r = page.rect
                scale = min(width / r.width, 2200 / r.height)
                pix = page.get_pixmap(matrix=pymupdf.Matrix(scale, scale), colorspace=pymupdf.csRGB, alpha=False)
                yield Image.frombytes('RGB', (pix.width, pix.height), pix.samples)
    else:
        for label, data in sources(path):
            try:
                with Image.open(io.BytesIO(data)) as img:
                    # Multi-page TIFF supported; animations intentionally use first frame.
                    count = getattr(img, 'n_frames', 1) if img.format == 'TIFF' else 1
                    for frame in range(count):
                        img.seek(frame)
                        yield ImageOps.exif_transpose(img).copy()
            except Exception as exc:
                raise ValueError(f'Não foi possível ler {label}: {exc}') from exc

def convert(source, destination, width=960, progress=lambda s: None):
    source, destination = Path(source), Path(destination)
    if width not in (480, 720, 960, 1200): raise ValueError('Largura inválida.')
    destination.mkdir(parents=True, exist_ok=True)
    if source.is_dir() and (destination.resolve() == source.resolve() or source.resolve() in destination.resolve().parents):
        raise ValueError('Escolha uma pasta de saída fora da pasta de origem.')
    title = re.sub(r'[<>:"/\\|?*\x00-\x1f]', '_', source.stem if source.is_file() else source.name).strip(' .') or 'Manga'
    target = destination / (title + '.cbz')
    if target.exists(): raise FileExistsError(f'{target.name} já existe. Renomeie ou escolha outra pasta.')
    fd, temp = tempfile.mkstemp(prefix='.mangadock-', suffix='.tmp', dir=destination)
    os.close(fd)
    try:
        count = 0
        with zipfile.ZipFile(temp, 'w', compression=zipfile.ZIP_STORED) as output:
            for count, img in enumerate(pages(source, width), 1):
                if count > MAX_PAGES: raise ValueError('Limite de 10.000 páginas por volume.')
                with img:
                    img.thumbnail((width, 2200), Image.Resampling.LANCZOS)
                    rgba = img.convert('RGBA')
                    rgb = Image.new('RGB', rgba.size, 'white')
                    rgb.paste(rgba, mask=rgba.getchannel('A'))
                    buf = io.BytesIO()
                    rgb.save(buf, 'JPEG', quality=88, optimize=True)
                    output.writestr(f'{count:05d}.jpg', buf.getvalue())
                    rgba.close(); rgb.close()
                progress(f'{title}: página {count}')
            if not count: raise ValueError('Nenhuma página encontrada.')
        # Exclusive creation protects an existing volume, including concurrent imports.
        try:
            with open(target, 'xb') as final, open(temp, 'rb') as src:
                import shutil
                shutil.copyfileobj(src, final)
        except FileExistsError:
            raise
        except Exception:
            target.unlink(missing_ok=True)
            raise
        return target, count
    finally:
        Path(temp).unlink(missing_ok=True)

def gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    import threading, queue
    root = tk.Tk(); root.title('MangaDock • Importador para PSP'); root.geometry('640x340')
    root.configure(bg='#111827')
    frame = ttk.Frame(root, padding=24); frame.pack(fill='both', expand=True)
    ttk.Label(frame, text='MangaDock', font=('Segoe UI', 24, 'bold')).pack(anchor='w')
    ttk.Label(frame, text='Prepare seus mangás e copie os CBZ para /MANGA no cartão do PSP.').pack(anchor='w', pady=(0, 15))
    dest = tk.StringVar(value=str(Path.home() / 'MangaDock'))
    ttk.Entry(frame, textvariable=dest).pack(fill='x')
    def choose():
        p = filedialog.askdirectory(title='Pasta de saída (pode ser MANGA no cartão)')
        if p: dest.set(p)
    ttk.Button(frame, text='Escolher pasta de saída', command=choose).pack(anchor='w', pady=5)
    width = tk.StringVar(value='960')
    ttk.Label(frame, text='Largura máxima da página (960 é o padrão):').pack(anchor='w')
    ttk.Combobox(frame, textvariable=width, values=('480', '720', '960', '1200'), state='readonly', width=12).pack(anchor='w')
    status = tk.StringVar(value='PDF • CBZ/ZIP • CBR/RAR* • CBT/TAR • imagens e pastas')
    events = queue.Queue(); buttons = []; busy = [False]
    def close():
        if busy[0]:
            messagebox.showinfo('Importação em andamento', 'Aguarde a importação terminar antes de fechar.')
        else: root.destroy()
    root.protocol('WM_DELETE_WINDOW', close)
    def start(folder=False):
        paths = [filedialog.askdirectory()] if folder else filedialog.askopenfilenames(title='Selecionar mangás')
        paths = [p for p in paths if p]
        if not paths: return
        output, size = dest.get().strip(), int(width.get())
        if not output:
            messagebox.showerror('Pasta de saída', 'Escolha uma pasta de saída.'); return
        busy[0] = True
        for b in buttons: b.configure(state='disabled')
        def work():
            results = []
            for p in paths:
                try:
                    target, count = convert(p, output, size, lambda msg: events.put(('status', msg)))
                    results.append(f'OK: {target.name} ({count} páginas)')
                except Exception as e: results.append(f'ERRO: {Path(p).name}: {e}')
            events.put(('done', '\n'.join(results)))
        threading.Thread(target=work, daemon=True).start()
    row = ttk.Frame(frame); row.pack(anchor='w', pady=12)
    for label, folder in [('Importar arquivos', False), ('Importar pasta', True)]:
        b = ttk.Button(row, text=label, command=lambda f=folder: start(f)); b.pack(side='left', padx=3); buttons.append(b)
    ttk.Label(frame, textvariable=status, wraplength=570).pack(anchor='w')
    def poll():
        while not events.empty():
            kind, msg = events.get()
            if kind == 'done':
                busy[0] = False
                for b in buttons: b.configure(state='normal')
                status.set('Importação concluída. Veja o resultado.'); messagebox.showinfo('Resultado', msg)
            else: status.set(msg)
        root.after(100, poll)
    poll(); root.mainloop()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', nargs='?'); parser.add_argument('--out', default='MangaDock-output'); parser.add_argument('--width', type=int, default=960)
    args = parser.parse_args()
    if args.source:
        result, count = convert(args.source, args.out, args.width, print)
        print(f'Concluído: {result} ({count} páginas)')
    else: gui()
