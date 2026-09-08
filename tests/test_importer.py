import unittest, tempfile, sys, zipfile, io, tarfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'desktop'))
from importer import convert, sources
from PIL import Image
import pymupdf

class ImportTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.out=self.root/'out'
    def tearDown(self): self.tmp.cleanup()
    def picture(self, color='white', size=(800,1200), fmt='PNG'):
        b=io.BytesIO();Image.new('RGB',size,color).save(b,fmt);return b.getvalue()
    def test_natural_order_and_hidden_files(self):
        p=self.root/'book.cbz'
        with zipfile.ZipFile(p,'w') as z:
            z.writestr('10.png',self.picture('blue'));z.writestr('2.png',self.picture('red'))
            z.writestr('__MACOSX/._1.png',b'invalid');z.writestr('notes.txt','ignore')
        dest,count=convert(p,self.out)
        self.assertEqual(count,2)
        with zipfile.ZipFile(dest) as z:
            self.assertEqual(z.namelist(),['00001.jpg','00002.jpg'])
            with Image.open(io.BytesIO(z.read('00001.jpg'))) as image:
                r,g,b=image.getpixel((0,0));self.assertGreater(r,240);self.assertLess(b,10)
    def test_pdf_pages_and_size(self):
        p=self.root/'two.pdf'
        with pymupdf.open() as d:
            d.new_page(width=600,height=900);d.new_page(width=600,height=6000);d.save(p)
        dest,count=convert(p,self.out)
        self.assertEqual(count,2)
        with zipfile.ZipFile(dest) as z:
            for n in z.namelist():
                with Image.open(io.BytesIO(z.read(n))) as im:
                    self.assertLessEqual(im.width,960);self.assertLessEqual(im.height,2200)
    def test_no_overwrite(self):
        p=self.root/'one.png';p.write_bytes(self.picture())
        dest,_=convert(p,self.out);original=dest.read_bytes()
        with self.assertRaises(FileExistsError):convert(p,self.out)
        self.assertEqual(original,dest.read_bytes())
    def test_failed_import_cleanup(self):
        p=self.root/'bad.cbz'
        with zipfile.ZipFile(p,'w') as z:z.writestr('1.png',self.picture());z.writestr('2.png',b'broken')
        with self.assertRaises(ValueError):convert(p,self.out)
        self.assertEqual(list(self.out.iterdir()),[])
    def test_tiff_multiframe_and_transparency(self):
        p=self.root/'multi.tiff'
        a=Image.new('RGBA',(100,200),(0,0,0,0));b=Image.new('RGBA',(100,200),'red')
        a.save(p,save_all=True,append_images=[b]);a.close();b.close()
        dest,count=convert(p,self.out);self.assertEqual(count,2)
        with zipfile.ZipFile(dest) as z:
            with Image.open(io.BytesIO(z.read('00001.jpg'))) as im:self.assertEqual(im.getpixel((0,0)),(255,255,255))
    def test_archive_paths_are_not_extracted(self):
        p=self.root/'paths.cbt'
        with tarfile.open(p,'w') as t:
            data=self.picture();i=tarfile.TarInfo('../../escape.png');i.size=len(data);t.addfile(i,io.BytesIO(data))
            safe=tarfile.TarInfo('1.png');safe.size=len(data);t.addfile(safe,io.BytesIO(data))
        dest,count=convert(p,self.out);self.assertEqual(count,1)
        self.assertFalse((self.root/'escape.png').exists())
    def test_encrypted_pdf(self):
        p=self.root/'secret.pdf'
        with pymupdf.open() as d:
            d.new_page();d.save(p,encryption=pymupdf.PDF_ENCRYPT_AES_256,owner_pw='owner',user_pw='secret')
        with self.assertRaisesRegex(ValueError,'senha'):convert(p,self.out)
    def test_directory_input_and_collision_guard(self):
        p=self.root/'pages';p.mkdir();(p/'1.webp').write_bytes(self.picture(fmt='WEBP'))
        _,count=convert(p,self.out);self.assertEqual(count,1)
        with self.assertRaises(ValueError):convert(p,p/'output')
    def test_empty_and_unsupported(self):
        p=self.root/'empty.zip'
        with zipfile.ZipFile(p,'w'):pass
        with self.assertRaisesRegex(ValueError,'Nenhuma'):convert(p,self.out)
        p=self.root/'book.cb7';p.write_bytes(b'')
        with self.assertRaisesRegex(ValueError,'Formato'):convert(p,self.out)

if __name__=='__main__':unittest.main()
