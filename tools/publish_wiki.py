"""Publishes docs/wiki/ to the GitHub wiki (https://github.com/CarlosNS/c6-fx-clock/wiki).

The wiki is a separate git repository; docs/wiki/ is its source, edited and reviewed like the code.
This replaces the wiki content with docs/wiki/ in one commit and pushes it.

    python tools/publish_wiki.py            # needs git with push access (gh auth setup-git)
    python tools/publish_wiki.py --dry-run  # show what would change, push nothing

GitHub only creates the wiki repository once its first page exists: if the push says "Repository
not found", create any page at https://github.com/CarlosNS/c6-fx-clock/wiki/_new and run this again.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs", "wiki")
WIKI = "https://github.com/CarlosNS/c6-fx-clock.wiki.git"
EMAIL = "19167897+CarlosNS@users.noreply.github.com"


def git(*args, cwd, check=True):
    return subprocess.run(["git", *args], cwd=cwd, check=check, text=True, capture_output=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("-m", "--message", default="Wiki: actualizada desde docs/wiki")
    a = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        tree = os.path.join(tmp, "wiki")
        r = git("clone", "--depth", "1", WIKI, tree, cwd=tmp, check=False)
        if r.returncode != 0:
            if "not found" in r.stderr.lower():
                sys.exit("La wiki aún no existe: crea una página cualquiera en "
                         "https://github.com/CarlosNS/c6-fx-clock/wiki/_new y vuelve a ejecutar esto.")
            sys.exit(r.stderr)
        for name in os.listdir(tree):
            if name != ".git":
                p = os.path.join(tree, name)
                shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
        shutil.copytree(SRC, tree, dirs_exist_ok=True)
        git("config", "core.autocrlf", "false", cwd=tree)
        git("config", "user.name", "CarlosNS", cwd=tree)
        git("config", "user.email", EMAIL, cwd=tree)
        git("add", "-A", cwd=tree)
        changes = git("status", "--short", cwd=tree).stdout
        if not changes:
            print("La wiki ya está al día.")
            return
        print(changes)
        if a.dry_run:
            print("--dry-run: no se ha publicado nada.")
            return
        git("commit", "-m", a.message, cwd=tree)
        branch = git("rev-parse", "--abbrev-ref", "HEAD", cwd=tree).stdout.strip()
        git("push", "origin", f"HEAD:{branch}", cwd=tree)
        print("Publicada: https://github.com/CarlosNS/c6-fx-clock/wiki")


if __name__ == "__main__":
    main()
