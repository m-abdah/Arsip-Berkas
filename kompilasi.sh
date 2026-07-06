#!/bin/bash

echo "Mengkompilasi adharc.c"
rm -f adharc

gcc adharc.c -o adharc -O2

if [ $? -ne 0 ]; then
    echo ""
    echo "Kompilasi GAGAL! Silakan cek pesan error di atas."
    read -p "Tekan Enter untuk keluar..."
    exit 1
fi

echo "Kompilasi BERHASIL! Berkas executable 'adharc' telah dibuat."
read -p "Tekan Enter untuk keluar..."
