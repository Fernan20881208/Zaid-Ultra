#!/usr/bin/env python3
"""Download and verify the two official Samsung source ZIPs via provided MediaFire pages.

Does not use MediaFire APIs requiring login, flash or modify a phone.
Fails closed on HTML interstitials, CAPTCHA, bad ZIPs, or checksum mismatches.
"""
import argparse
import base64
import hashlib
from html.parser import HTMLParser
import os
from pathlib import Path
import re
import shutil
import time
import urllib.error
import urllib.parse
import urllib.request
import http.cookiejar
import zipfile

# Share-page URLs provided by the repository owner, not guessed direct-download links.
BUNDLE_URL = "https://www.mediafire.com/file/jwwfja7b8dqbswj/SM-A175F.zip/file"
BASE_URL = "https://www.mediafire.com/file/dtt5mrk0wzevk9t/SM-A175F_16_Opensource.zip/file"

BASE_NAME = "SM-A175F_16_Opensource.zip"
BUNDLE_NAME = "SM-A175F.zip"
PATCH_NAME = "SM-A175F_16_Opensource_A175FXXS7CZI4_A175NKSS7CZI2.zip"
BASE_SHA256 = "4fa6e41f63151728277f49842d3fe1d59d6cfaaf853a161c7c2ed98f906727dd"
BUNDLE_SHA256 = "8d05b158f0b393ef0f62771a5dc29f96298813a793cdcb0bd6acf442aa985b6a"
PATCH_SHA256 = "643bde1afbf8dd17f98962314245818a0057ed46d6a22503f7ddeacd0478495b"

HEADERS = {
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                  "(KHTML, like Gecko) Chrome/128.0 Safari/537.36",
    "Accept-Language": "en-US,en;q=0.9",
}


def host_allowed(link):
    p = urllib.parse.urlparse(link)
    host = (p.hostname or "").lower()
    return p.scheme == "https" and (
        host == "mediafire.com" or host.endswith(".mediafire.com")
        or host == "mediafireusercontent.com" or host.endswith(".mediafireusercontent.com")
    )


def direct_link(link):
    return host_allowed(link) and (
        re.match(r"^download\d*\.mediafire\.com$", urllib.parse.urlparse(link).hostname or "")
        or "mediafireusercontent.com" in (urllib.parse.urlparse(link).hostname or "")
    )


class MediaFireButtons(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []

    def handle_starttag(self, tag, attrs):
        if tag.lower() != "a":
            return
        attrs = dict(attrs)
        if attrs.get("id") != "downloadButton":
            return
        scrambled = attrs.get("data-scrambled-url")
        if scrambled:
            try:
                data = base64.b64decode(scrambled + ("=" * (-len(scrambled) % 4)))
                self.links.append(data.decode("utf-8"))
            except (ValueError, UnicodeError):
                pass
        href = attrs.get("href")
        if href:
            self.links.append(href)


def extract_download_url(html_text):
    p = MediaFireButtons()
    p.feed(html_text)
    # Alternate MediaFire templates embed the direct URL rather than an anchor.
    p.links.extend(re.findall(r"https://download\d+\.mediafire\.com/[^\s'\"<>\\]+", html_text))
    for link in p.links:
        link = link.replace("&amp;", "&").replace(r"\/", "/")
        if direct_link(link):
            return link
    raise RuntimeError(
        "MediaFire no ofrecio un enlace directo (posible CAPTCHA, limite, "
        "proteccion antibot o cambio de pagina). No se descargara HTML."
    )


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def check_file(path, expected):
    if not path.is_file():
        return False
    if sha256(path) == expected:
        return True
    raise RuntimeError(f"Existe {path}, pero el SHA-256 es incorrecto. No sobrescribir automaticamente.")


def download_one(opener, page_url, output, expected_sha, max_bytes):
    if check_file(output, expected_sha):
        print(f"Ya verificado: {output.name}", flush=True)
        return
    for attempt in range(1, 4):
        tmp = output.with_suffix(output.suffix + ".part")
        try:
            if tmp.exists():
                tmp.unlink()
            with opener.open(urllib.request.Request(page_url, headers=HEADERS), timeout=40) as r:
                if r.status != 200:
                    raise RuntimeError(f"MediaFire pagina HTTP {r.status}")
                page = r.read(5 * 1024 * 1024 + 1)
                if len(page) > 5 * 1024 * 1024:
                    raise RuntimeError("Pagina MediaFire inusualmente grande")
            link = extract_download_url(page.decode("utf-8", "replace"))
            print(f"Intento {attempt}: obteniendo {output.name}", flush=True)
            headers = dict(HEADERS)
            headers["Referer"] = page_url
            with opener.open(urllib.request.Request(link, headers=headers), timeout=120) as r:
                if r.status != 200:
                    raise RuntimeError(f"Descarga HTTP {r.status}")
                total = 0
                h = hashlib.sha256()
                with tmp.open("wb") as out:
                    while True:
                        data = r.read(1024 * 1024)
                        if not data:
                            break
                        if total == 0 and not data.startswith(b"PK\x03\x04"):
                            raise RuntimeError("La respuesta no es un ZIP: MediaFire entrego HTML u otro contenido")
                        total += len(data)
                        if total > max_bytes:
                            raise RuntimeError("Archivo mayor al limite de seguridad configurado")
                        h.update(data)
                        out.write(data)
            actual = h.hexdigest()
            if actual != expected_sha:
                raise RuntimeError(f"SHA-256 incorrecto en {output.name}: {actual}")
            if not zipfile.is_zipfile(tmp):
                raise RuntimeError("ZIP corrupto")
            os.replace(tmp, output)
            print(f"Verificado SHA-256: {output.name} ({total} bytes)", flush=True)
            return
        except (OSError, RuntimeError, urllib.error.URLError) as e:
            tmp.unlink(missing_ok=True)
            print(f"Intento {attempt} fallo: {e}", flush=True)
            if attempt == 3:
                raise RuntimeError(
                    f"No se pudo descargar {output.name} desde MediaFire. "
                    "Revise si pide CAPTCHA o limita GitHub Actions."
                ) from e
            time.sleep(attempt * 3)


def extract_patch(bundle_path, dest):
    if check_file(dest, PATCH_SHA256):
        print("Overlay CZI4 ya verificado", flush=True)
        return
    with zipfile.ZipFile(bundle_path) as z:
        if PATCH_NAME not in z.namelist():
            raise RuntimeError("El ZIP combinado no contiene la actualizacion CZI4 requerida")
        info = z.getinfo(PATCH_NAME)
        if info.file_size > 10 * 1024 * 1024:
            raise RuntimeError("Overlay CZI4 demasiado grande")
        with z.open(info) as src, dest.open("wb") as out:
            shutil.copyfileobj(src, out)
    try:
        if sha256(dest) != PATCH_SHA256:
            raise RuntimeError("SHA-256 incorrecto en el overlay CZI4 extraido")
        if not zipfile.is_zipfile(dest):
            raise RuntimeError("Overlay CZI4 no es un ZIP valido")
    except Exception:
        dest.unlink(missing_ok=True)
        raise
    print(f"Overlay CZI4 extraido y verificado: {dest}", flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out-dir", type=Path, required=True)
    args = p.parse_args()
    out_dir = args.out_dir.expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    download_one(opener, BUNDLE_URL, out_dir / BUNDLE_NAME, BUNDLE_SHA256, 10 * 1024 * 1024)
    download_one(opener, BASE_URL, out_dir / BASE_NAME, BASE_SHA256, 500 * 1024 * 1024)
    extract_patch(out_dir / BUNDLE_NAME, out_dir / PATCH_NAME)
    print("Fuentes base y actualizacion CZI4 verificadas; no se han compilado modulos.", flush=True)


if __name__ == "__main__":
    main()
