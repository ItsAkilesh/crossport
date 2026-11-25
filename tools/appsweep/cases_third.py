"""
File: tools/appsweep/cases_third.py
Purpose: The third set of sweep cases: binutils, more compressors, XML and HTML tools, image and PDF tools, audio generation and date tools
Author: Akilesh
Date: 2026-11-24
"""

from pathlib import Path

from cases import SAMPLE_TEXT, case

DATA = Path(__file__).resolve().parent / "data"
TINY_OBJECT = (DATA / "tiny_o.b64").read_text(encoding="ascii").strip()
PNG_1X1 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
GIF_1X1 = "R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"
ASSEMBLY = ".text\n.globl answer\nanswer:\n\tmovl $42, %eax\n\tret\n"
XML = "<root><item id=\"1\">alpha</item><item id=\"2\">beta</item></root>\n"
XSL = ("<xsl:stylesheet version=\"1.0\" xmlns:xsl=\"http://www.w3.org/1999/XSL/Transform\">"
       "<xsl:output method=\"text\"/><xsl:template match=\"/root\"><xsl:for-each select=\"item\">"
       "<xsl:value-of select=\"@id\"/>=<xsl:value-of select=\".\"/><xsl:text>;</xsl:text></xsl:for-each>"
       "</xsl:template></xsl:stylesheet>\n")
HTML = "<html><head><title>t</title></head><body><p>one<p>two</body></html>\n"
SVG_LIKE_PATH = "0 0 m 10 0 l 10 10 l 0 10 l cp\n"
CSV = "3,b\n1,a\n2,c\n"
PDF = ("%PDF-1.1\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj\n"
       "3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>endobj\n"
       "4 0 obj<</Length 44>>stream\nBT /F1 12 Tf 10 50 Td (Crossport) Tj ET\nendstream endobj\n"
       "5 0 obj<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>endobj\ntrailer<</Root 1 0 R/Size 6>>\n%%EOF\n")


def binutils() -> list[dict]:
    files = {"t.o": TINY_OBJECT}
    return [
        case("nm", "symbols", ["t.o"], files_b64=files),
        case("objdump", "disassemble", ["-d", "t.o"], files_b64=files),
        case("readelf", "header", ["-h", "t.o"], files_b64=files),
        case("size", "sections", ["t.o"], files_b64=files),
        case("strings", "text", ["-a", "t.o"], files_b64=files),
        case("strip", "debug", ["-o", "stripped.o", "t.o"], files_b64=files),
        case("ar", "archive-list", ["rcs", "lib.a", "t.o"], files_b64=files, ignore_files=["lib.a"]),
        case("as", "assemble", ["-o", "a.o", "a.s"], files={"a.s": ASSEMBLY}, ignore_files=["a.o"]),
        case("ld", "version", ["--version"]),
        case("objcopy", "copy", ["t.o", "copy.o"], files_b64=files),
    ]


def compressors() -> list[dict]:
    files = {"in.txt": SAMPLE_TEXT * 60}
    return [
        case("7zr", "archive", ["a", "-bd", "-y", "out.7z", "in.txt"], files=files, ignore_files=["out.7z"], binary="/usr/lib/7zip/7zr"),
        case("pigz", "compress", ["-k", "-n", "in.txt"], files=files),
        case("brotli", "compress", ["-k", "-q", "5", "in.txt"], files=files),
        case("lzop", "compress", ["-q", "-n", "in.txt"], files=files, ignore_files=["in.txt.lzo"]),
        case("lzip", "compress", ["-k", "in.txt"], files=files),
        case("bzip2", "test", ["-t", "missing.bz2"]),
    ]


def documents_and_data() -> list[dict]:
    return [
        case("xmllint", "format", ["--format", "doc.xml"], files={"doc.xml": XML}),
        case("xmllint", "xpath", ["--xpath", "string(//item[@id='2'])", "doc.xml"], files={"doc.xml": XML}),
        case("xsltproc", "transform", ["style.xsl", "doc.xml"], files={"doc.xml": XML, "style.xsl": XSL}),
        case("xmlstarlet", "select", ["sel", "-t", "-v", "//item[@id='1']", "doc.xml"], files={"doc.xml": XML}),
        case("tidy", "clean", ["-q", "-e", "page.html"], files={"page.html": HTML}),
        case("datamash", "sum", ["-t", ",", "sum", "1"], CSV),
        case("potrace", "trace", ["-s", "--flat", "-o", "out.svg", "shape.pbm"], files={"shape.pbm": "P1\n4 4\n1 1 1 1\n1 0 0 1\n1 0 0 1\n1 1 1 1\n"},
             ignore_files=["out.svg"]),
        case("pdfinfo", "info", ["doc.pdf"], files={"doc.pdf": PDF}),
        case("pdftotext", "extract", ["doc.pdf", "-"], files={"doc.pdf": PDF}),
        case("qpdf", "check", ["--check", "doc.pdf"], files={"doc.pdf": PDF}),
        case("ncal", "month", ["-b", "2 2024"]),
        case("cal", "year-month", ["3", "2024"]),
    ]


def images_and_audio() -> list[dict]:
    return [
        case("optipng", "optimize", ["-quiet", "-out", "small.png", "in.png"], files_b64={"in.png": PNG_1X1}),
        case("pngcrush", "reduce", ["-s", "in.png", "out.png"], files_b64={"in.png": PNG_1X1}, ignore_files=["out.png"]),
        case("gifsicle", "info", ["--info", "in.gif"], files_b64={"in.gif": GIF_1X1}),
        case("exiv2", "summary", ["in.png"], files_b64={"in.png": PNG_1X1}),
        case("cwebp", "encode", ["-quiet", "in.png", "-o", "out.webp"], files_b64={"in.png": PNG_1X1}),
        case("sox", "generate", ["-R", "-n", "-r", "8000", "-b", "8", "-c", "1", "tone.wav", "synth", "0.05", "sine", "440"]),
        case("sox", "version", ["--version"]),
    ]


def third_cases() -> list[dict]:
    return binutils() + compressors() + documents_and_data() + images_and_audio()
