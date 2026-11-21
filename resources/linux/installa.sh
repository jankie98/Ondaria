#!/bin/sh
# Aggiunge Ondaria al menu delle applicazioni dell'utente, usando l'eseguibile
# che si trova in questa cartella. Non servono permessi di amministratore.
set -e
here=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}

mkdir -p "$data/applications" "$data/icons/hicolor/scalable/apps" "$data/icons/hicolor/256x256/apps"
cp "$here/ondaria.svg" "$data/icons/hicolor/scalable/apps/ondaria.svg"
cp "$here/ondaria.png" "$data/icons/hicolor/256x256/apps/ondaria.png"
sed "s|@ONDARIA_EXEC@|\"$here/Ondaria\"|" "$here/ondaria.desktop.in" > "$data/applications/ondaria.desktop"

command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$data/applications" || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "$data/icons/hicolor" || true
echo "Ondaria è nel menu delle applicazioni."
