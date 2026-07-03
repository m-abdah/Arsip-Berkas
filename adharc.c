#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#ifdef _WIN32
    #include <windows.h>
    #define DIR_SEPARATOR '\\'
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <sys/mman.h>
    #include <fcntl.h>
    #define DIR_SEPARATOR '/'
#endif

#define SIGNATURE "ABDAH"
#define SIGNATURE_LEN 5
#define VERSI "5.0.0"

// Flag enkripsi
#define FLAG_NORMAL  0
#define FLAG_ENKRIP  1

// Buffer sizes
#define CHUNK_SIZE    65536   // 64KB chunk untuk I/O
#define STACK_BUFFER  65536   // 64KB stack buffer
#define IO_BUFFER     (1024 * 1024)  // 1MB I/O buffer

// ============================================================
//                    CRC32 (IEEE 802.3)
// ============================================================
static uint32_t crc32_table[256];
static int crc32_initialized = 0;

void crc32_init() {
    if (crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

uint32_t crc32_compute(const uint8_t *data, size_t len) {
    crc32_init();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

// Update CRC32 secara incremental (untuk streaming)
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc32_init();
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

uint32_t crc32_finalize(uint32_t crc) {
    return crc ^ 0xFFFFFFFF;
}

// ============================================================
//                    FORMAT UKURAN (B, KB, MB, GB, TB)
// ============================================================
void format_ukuran(size_t bytes, char *output, size_t output_size) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = (double)bytes;
    
    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }
    
    if (unit_index == 0) {
        snprintf(output, output_size, "%.0f %s", size, units[unit_index]);
    } else if (size < 10.0) {
        snprintf(output, output_size, "%.2f %s", size, units[unit_index]);
    } else if (size < 100.0) {
        snprintf(output, output_size, "%.1f %s", size, units[unit_index]);
    } else {
        snprintf(output, output_size, "%.0f %s", size, units[unit_index]);
    }
}

// ============================================================
//                    XOR ENKRIPSI / DEKRIPSI
// ============================================================
void xor_cipher(uint8_t *data, size_t len, const char *key, size_t key_len) {
    for (size_t i = 0; i < len; i++) {
        data[i] ^= key[i % key_len];
    }
}

// ============================================================
//                    FUNGSI BANTU: STRING
// ============================================================
char* my_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

char* gabung_path(const char *folder, const char *file) {
    size_t len_folder = strlen(folder);
    size_t len_file = strlen(file);
    size_t perlu_sep = (len_folder > 0 && folder[len_folder-1] != DIR_SEPARATOR) ? 1 : 0;
    size_t total = len_folder + len_file + perlu_sep + 1;
    char *hasil = (char*)malloc(total);
    if (hasil == NULL) return NULL;
    strcpy(hasil, folder);
    if (perlu_sep) {
        hasil[len_folder] = DIR_SEPARATOR;
        hasil[len_folder + 1] = '\0';
    }
    strcat(hasil, file);
    return hasil;
}

int is_directory(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributes(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
#endif
}

// ============================================================
//                    TULIS NAMA (DINAMIS)
// ============================================================
void tulis_nama(FILE *fp, const char *nama, const char *key, size_t key_len, int enkrip) {
    uint16_t panjang = (uint16_t)strlen(nama);

    if (enkrip && key_len > 0) {
        char *nama_enc = my_strdup(nama);
        xor_cipher((uint8_t*)nama_enc, panjang, key, key_len);
        fwrite(&panjang, sizeof(uint16_t), 1, fp);
        fwrite(nama_enc, 1, panjang, fp);
        free(nama_enc);
    } else {
        fwrite(&panjang, sizeof(uint16_t), 1, fp);
        fwrite(nama, 1, panjang, fp);
    }
}

// ============================================================
//                    BACA NAMA (DINAMIS)
// ============================================================
char* baca_nama(FILE *fp, const char *key, size_t key_len, int enkrip) {
    uint16_t panjang;
    if (fread(&panjang, sizeof(uint16_t), 1, fp) != 1) return NULL;

    char *nama = (char*)malloc(panjang + 1);
    if (nama == NULL) return NULL;

    fread(nama, 1, panjang, fp);
    nama[panjang] = '\0';

    if (enkrip && key_len > 0) {
        xor_cipher((uint8_t*)nama, panjang, key, key_len);
    }

    return nama;
}

// ============================================================
//                    VALIDASI SIGNATURE
// ============================================================
int validasi_signature(FILE *fp) {
    char sig[SIGNATURE_LEN];
    fread(sig, 1, SIGNATURE_LEN, fp);
    if (memcmp(sig, SIGNATURE, SIGNATURE_LEN) != 0) {
        fprintf(stderr, "❌ Bukan container ABDAH!\n");
        return 0;
    }
    return 1;
}

// ============================================================
//                    TULIS SATU BERKAS (OPTIMIZED STREAMING)
// ============================================================
int tulis_satu_berkas(FILE *keluar, const char *path_asli, const char *nama_dalam,
                      const char *key, size_t key_len, int enkrip) {

#ifdef _WIN32
    // Windows: gunakan file I/O biasa dengan buffer besar
    FILE *masuk = fopen(path_asli, "rb");
    if (masuk == NULL) {
        fprintf(stderr, "⚠ Gagal buka: %s\n", path_asli);
        return 0;
    }
    
    // Set buffer besar untuk input
    setvbuf(masuk, NULL, _IOFBF, IO_BUFFER);
    
    fseek(masuk, 0, SEEK_END);
    size_t ukuran = (size_t)ftell(masuk);
    rewind(masuk);
    
    // Tulis nama
    tulis_nama(keluar, nama_dalam, key, key_len, enkrip);
    
    // Tulis ukuran (CRC tidak disimpan di header per-berkas)
    fwrite(&ukuran, sizeof(size_t), 1, keluar);
    
    // Streaming: baca -> (enkripsi) -> tulis dalam chunk
    uint8_t chunk[CHUNK_SIZE];
    size_t total_read = 0;
    
    while (total_read < ukuran) {
        size_t to_read = (ukuran - total_read < CHUNK_SIZE) ? 
                         (ukuran - total_read) : CHUNK_SIZE;
        size_t read = fread(chunk, 1, to_read, masuk);
        
        if (read == 0) break;
        
        // Enkripsi jika perlu
        if (enkrip && key_len > 0) {
            for (size_t i = 0; i < read; i++) {
                chunk[i] ^= key[(total_read + i) % key_len];
            }
        }
        
        fwrite(chunk, 1, read, keluar);
        total_read += read;
    }
    
    fclose(masuk);
    
#else
    // Linux/Mac: gunakan memory-mapped I/O untuk performa maksimal
    int fd = open(path_asli, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "⚠ Gagal buka: %s\n", path_asli);
        return 0;
    }
    
    struct stat st;
    fstat(fd, &st);
    size_t ukuran = (size_t)st.st_size;
    
    // Tulis nama
    tulis_nama(keluar, nama_dalam, key, key_len, enkrip);
    
    // Tulis ukuran (CRC tidak disimpan di header per-berkas)
    fwrite(&ukuran, sizeof(size_t), 1, keluar);
    
    // Memory-map file untuk pembacaan cepat
    uint8_t *data = (uint8_t*)mmap(NULL, ukuran, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        // Fallback ke I/O biasa jika mmap gagal
        close(fd);
        FILE *masuk = fopen(path_asli, "rb");
        if (!masuk) return 0;
        
        setvbuf(masuk, NULL, _IOFBF, IO_BUFFER);
        
        uint8_t chunk[CHUNK_SIZE];
        size_t total_read = 0;
        
        while (total_read < ukuran) {
            size_t to_read = (ukuran - total_read < CHUNK_SIZE) ? 
                             (ukuran - total_read) : CHUNK_SIZE;
            size_t read = fread(chunk, 1, to_read, masuk);
            
            if (read == 0) break;
            
            if (enkrip && key_len > 0) {
                for (size_t i = 0; i < read; i++) {
                    chunk[i] ^= key[(total_read + i) % key_len];
                }
            }
            
            fwrite(chunk, 1, read, keluar);
            total_read += read;
        }
        
        fclose(masuk);
    } else {
        // Data sudah di-mmap, tulis langsung dengan enkripsi jika perlu
        if (enkrip && key_len > 0) {
            // Perlu enkripsi, proses dalam chunk
            for (size_t offset = 0; offset < ukuran; offset += CHUNK_SIZE) {
                size_t chunk_size = (ukuran - offset < CHUNK_SIZE) ? 
                                   (ukuran - offset) : CHUNK_SIZE;
                
                // Enkripsi chunk dan tulis
                uint8_t enc_chunk[CHUNK_SIZE];
                for (size_t i = 0; i < chunk_size; i++) {
                    enc_chunk[i] = data[offset + i] ^ key[(offset + i) % key_len];
                }
                fwrite(enc_chunk, 1, chunk_size, keluar);
            }
        } else {
            // Tanpa enkripsi, tulis langsung dari memory
            fwrite(data, 1, ukuran, keluar);
        }
        
        munmap(data, ukuran);
    }
    
    close(fd);
#endif
    
    char ukuran_str[32];
    format_ukuran(ukuran, ukuran_str, sizeof(ukuran_str));
    fprintf(stderr, "✓ %s (%s)\n", nama_dalam, ukuran_str);
    return 1;
}

// ============================================================
//                    SCAN DIREKTORI (DINAMIS)
// ============================================================
#ifndef _WIN32
size_t scan_direktori(const char *folder, const char *prefix,
                      char ***daftar_path, char ***daftar_nama,
                      size_t *kapasitas, size_t *jumlah) {
    DIR *dir = opendir(folder);
    if (dir == NULL) {
        fprintf(stderr, "⚠ Gagal buka direktori: %s\n", folder);
        return 0;
    }

    struct dirent *entry;
    size_t ditambahkan = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char *path_asli = gabung_path(folder, entry->d_name);
        char *nama_dalam = (prefix[0] == '\0') ? my_strdup(entry->d_name)
                                                : gabung_path(prefix, entry->d_name);

        if (is_directory(path_asli)) {
            ditambahkan += scan_direktori(path_asli, nama_dalam,
                                          daftar_path, daftar_nama,
                                          kapasitas, jumlah);
            free(path_asli);
            free(nama_dalam);
        } else {
            if (*jumlah >= *kapasitas) {
                *kapasitas *= 2;
                *daftar_path = (char**)realloc(*daftar_path, *kapasitas * sizeof(char*));
                *daftar_nama = (char**)realloc(*daftar_nama, *kapasitas * sizeof(char*));
            }
            (*daftar_path)[*jumlah] = path_asli;
            (*daftar_nama)[*jumlah] = nama_dalam;
            (*jumlah)++;
            ditambahkan++;
        }
    }
    closedir(dir);
    return ditambahkan;
}
#else
size_t scan_direktori(const char *folder, const char *prefix,
                      char ***daftar_path, char ***daftar_nama,
                      size_t *kapasitas, size_t *jumlah) {
    char *pattern = gabung_path(folder, "*");
    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(pattern, &findData);
    free(pattern);

    if (hFind == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "⚠ Gagal buka direktori: %s\n", folder);
        return 0;
    }

    size_t ditambahkan = 0;
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;

        char *path_asli = gabung_path(folder, findData.cFileName);
        char *nama_dalam = (prefix[0] == '\0') ? my_strdup(findData.cFileName)
                                                : gabung_path(prefix, findData.cFileName);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ditambahkan += scan_direktori(path_asli, nama_dalam,
                                          daftar_path, daftar_nama,
                                          kapasitas, jumlah);
            free(path_asli);
            free(nama_dalam);
        } else {
            if (*jumlah >= *kapasitas) {
                *kapasitas *= 2;
                *daftar_path = (char**)realloc(*daftar_path, *kapasitas * sizeof(char*));
                *daftar_nama = (char**)realloc(*daftar_nama, *kapasitas * sizeof(char*));
            }
            (*daftar_path)[*jumlah] = path_asli;
            (*daftar_nama)[*jumlah] = nama_dalam;
            (*jumlah)++;
            ditambahkan++;
        }
    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
    return ditambahkan;
}
#endif

// ============================================================
//                    PROSES INPUT & TULIS
// ============================================================
int proses_input_dan_tulis(FILE *keluar,
                           char *file_list[], size_t jml_file,
                           char *dir_list[],  size_t jml_dir,
                           const char *key) {

    size_t key_len = key ? strlen(key) : 0;
    int enkrip = (key_len > 0) ? FLAG_ENKRIP : FLAG_NORMAL;

    size_t kapasitas = 256;
    size_t total_file = 0;
    char **daftar_path = (char**)malloc(kapasitas * sizeof(char*));
    char **daftar_nama = (char**)malloc(kapasitas * sizeof(char*));

    // Proses file (-f)
    for (size_t i = 0; i < jml_file; i++) {
        if (is_directory(file_list[i])) {
            fprintf(stderr, "⚠ \"%s\" adalah direktori, gunakan -d\n", file_list[i]);
            continue;
        }
        if (total_file >= kapasitas) {
            kapasitas *= 2;
            daftar_path = (char**)realloc(daftar_path, kapasitas * sizeof(char*));
            daftar_nama = (char**)realloc(daftar_nama, kapasitas * sizeof(char*));
        }
        daftar_path[total_file] = my_strdup(file_list[i]);
        const char *nama_saja = strrchr(file_list[i], DIR_SEPARATOR);
        daftar_nama[total_file] = nama_saja ? my_strdup(nama_saja + 1) : my_strdup(file_list[i]);
        total_file++;
    }

    // Proses direktori (-d)
    for (size_t i = 0; i < jml_dir; i++) {
        if (!is_directory(dir_list[i])) {
            fprintf(stderr, "⚠ \"%s\" bukan direktori, gunakan -f\n", dir_list[i]);
            continue;
        }
        fprintf(stderr, "📁 Memindai direktori: %s\n", dir_list[i]);
        const char *nama_folder = strrchr(dir_list[i], DIR_SEPARATOR);
        nama_folder = nama_folder ? nama_folder + 1 : dir_list[i];
        size_t ditambah = scan_direktori(dir_list[i], nama_folder,
                                          &daftar_path, &daftar_nama,
                                          &kapasitas, &total_file);
        fprintf(stderr, "   ✅ %zu file ditemukan\n\n", ditambah);
    }

    if (total_file == 0) {
        fprintf(stderr, "❌ Tidak ada file untuk diarsipkan\n");
        free(daftar_path);
        free(daftar_nama);
        return 0;
    }

    fprintf(stderr, "📦 Menulis %zu file ke container...\n", total_file);
    if (enkrip) fprintf(stderr, "🔐 Enkripsi XOR AKTIF (password: %zu karakter)\n\n", key_len);
    else fprintf(stderr, "🔓 Tanpa enkripsi\n\n");

    // Set buffer output besar
    setvbuf(keluar, NULL, _IOFBF, IO_BUFFER);

    // TULIS HEADER
    fwrite(SIGNATURE, 1, SIGNATURE_LEN, keluar);        // "ABDAH"
    uint8_t flag = (uint8_t)enkrip;
    fwrite(&flag, sizeof(uint8_t), 1, keluar);          // flag enkripsi
    fwrite(&total_file, sizeof(size_t), 1, keluar);     // jumlah file

    for (size_t i = 0; i < total_file; i++) {
        tulis_satu_berkas(keluar, daftar_path[i], daftar_nama[i], key, key_len, enkrip);
        free(daftar_path[i]);
        free(daftar_nama[i]);
    }

    free(daftar_path);
    free(daftar_nama);
    return 1;
}

// ============================================================
//                    MINTA PASSWORD
// ============================================================
char* minta_password(const char *prompt) {
    fprintf(stderr, "%s", prompt);

    size_t kap = 64;
    size_t len = 0;
    char *pass = (char*)malloc(kap);
    if (pass == NULL) return NULL;

    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
        if (len + 1 >= kap) {
            kap *= 2;
            pass = (char*)realloc(pass, kap);
            if (pass == NULL) return NULL;
        }
        pass[len++] = (char)c;
    }
    pass[len] = '\0';

    // Handle EOF di stdin (misal piped input)
    if (len == 0 && c == EOF) {
        free(pass);
        return NULL;
    }

    return pass;
}

// ============================================================
//                    BACA / LIST ISI (DENGAN CRC32)
// ============================================================
int baca_multi_berkas(const char *nama_container, const char *key) {
    FILE *masuk = fopen(nama_container, "rb");
    if (masuk == NULL) {
        fprintf(stderr, "⚠ Tidak bisa membuka: %s\n", nama_container);
        return 0;
    }

    // Set buffer input besar
    setvbuf(masuk, NULL, _IOFBF, IO_BUFFER);

    if (!validasi_signature(masuk)) { fclose(masuk); return 0; }

    // Baca flag enkripsi
    uint8_t flag;
    fread(&flag, sizeof(uint8_t), 1, masuk);

    size_t key_len = key ? strlen(key) : 0;

    // Jika container terenkripsi tapi user tidak kasih password
    if (flag == FLAG_ENKRIP && key_len == 0) {
        fprintf(stderr, "🔐 Container ini TERENKRIPSI!\n");
        fprintf(stderr, "   Gunakan: abdah -p \"password\" -l %s\n\n", nama_container);
        fclose(masuk);
        return 0;
    }

    if (flag == FLAG_ENKRIP) {
        printf("🔐 Container terenkripsi (XOR)\n\n");
    } else {
        printf("🔓 Container normal (tanpa enkripsi)\n\n");
    }

    size_t jumlah;
    fread(&jumlah, sizeof(size_t), 1, masuk);

    size_t total_ukuran = 0;
    size_t file_count = 0;
    size_t depth_max = 0;
    int error_crc = 0;

    printf("📦 Container \"%s\" — %zu entri:\n\n", nama_container, jumlah);
    printf("%-6s %-42s %12s  %-10s  %s\n", "No", "Nama", "Ukuran", "CRC32", "Status");
    printf("───────────────────────────────────────────────────────────────────────────────\n");

    for (size_t i = 0; i < jumlah; i++) {
        char *nama = baca_nama(masuk, key, key_len, flag == FLAG_ENKRIP);
        if (nama == NULL) break;

        size_t ukuran;
        fread(&ukuran, sizeof(size_t), 1, masuk);

        // Hitung CRC32 dengan streaming saat membaca data
        uint32_t crc = 0xFFFFFFFF;
        uint8_t chunk[CHUNK_SIZE];
        size_t total_read = 0;
        
        while (total_read < ukuran) {
            size_t to_read = (ukuran - total_read < CHUNK_SIZE) ? 
                             (ukuran - total_read) : CHUNK_SIZE;
            size_t read = fread(chunk, 1, to_read, masuk);
            
            if (read == 0) break;
            
            // Dekrip jika perlu (di tempat)
            if (flag == FLAG_ENKRIP && key_len > 0) {
                for (size_t j = 0; j < read; j++) {
                    chunk[j] ^= key