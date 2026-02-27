#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <curl/curl.h>
#include <sys/stat.h>

#define SCREEN_W 1280
#define SCREEN_H 720
#define DEFAULT_INTERVAL_MINS 5
#define UI_HIDE_DELAY_MS 4000

#define BTN_A       0
#define BTN_B       1
#define BTN_PLUS    10
#define BTN_MINUS   11
#define BTN_DLEFT   12
#define BTN_DRIGHT  14

#define ICON_DLEFT  "\xee\x80\x80"   // U+E000
#define ICON_DRIGHT "\xee\x80\x81"   // U+E001
#define ICON_PLUS   "\xee\x80\x82"   // U+E002
#define ICON_MINUS  "\xee\x80\x83"   // U+E003
#define ICON_A      "\xee\x80\x84"   // U+E004
#define ICON_B      "\xee\x80\x85"   // U+E005

#define MAX_CATEGORIES 32
#define CONFIG_PATH "sdmc:/config/NXPhotoFrame/config.ini"
#define CONFIG_DIR  "sdmc:/config/NXPhotoFrame"

typedef struct {
    char name[64];
    char url[256];       // NULL-equivalent: empty string
    char localpath[256]; // NULL-equivalent: empty string
} Category;

static Category CATEGORIES[MAX_CATEGORIES];
static int NUM_CATEGORIES = 0;

typedef struct {
    unsigned char *data;
    size_t size;
} MemoryBuffer;

// Create directory and/or file if it doesn't exist
void write_default_config(void) {
    mkdir(CONFIG_DIR, 0777);

    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) return;

    fprintf(f, "[Categories]\n");
	fprintf(f, "Album = local://sdmc:/Nintendo/Album/\n");
	fprintf(f, "Video Games = https://gandalfsax.com/images/vg.jpg\n");
	fprintf(f, "Halloween = https://gandalfsax.com/images/hw.jpg\n");
    fprintf(f, "Ancient Girls = https://gandalfsax.com/images/ag.jpg\n");
    fprintf(f, "Gaming Girls = https://gandalfsax.com/images/gg.jpg\n");
    fprintf(f, "Lofi Time = https://gandalfsax.com/images/lt.jpg\n");
    fprintf(f, "Waifu & Chill = https://gandalfsax.com/images/wac.jpg\n");
	fprintf(f, "All Girls = https://gandalfsax.com/images/girls.jpg\n");
    fclose(f);
}

void load_config(void) {
    // If config doesn't exist, write defaults first
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) {
        write_default_config();
        f = fopen(CONFIG_PATH, "r");
        if (!f) return; // SD card issue
    }

    NUM_CATEGORIES = 0;
    char line[320];
    int in_categories = 0;

    while (fgets(line, sizeof(line), f) && NUM_CATEGORIES < MAX_CATEGORIES) {
        // Trim newline
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *cr = strchr(line, '\r');
        if (cr) *cr = 0;

        // Skip empty lines and comments
        if (line[0] == 0 || line[0] == ';' || line[0] == '#') continue;

        // Section header
        if (line[0] == '[') {
            in_categories = (strncmp(line, "[Categories]", 12) == 0);
            continue;
        }

        if (!in_categories) continue;

        // Parse key = value
        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = 0;
        char *key = line;
        char *val = eq + 1;

        // Trim whitespace from key
        while (*key == ' ') key++;
        char *end = key + strlen(key) - 1;
        while (end > key && *end == ' ') { *end = 0; end--; }

        // Trim whitespace from value
        while (*val == ' ') val++;
        end = val + strlen(val) - 1;
        while (end > val && *end == ' ') { *end = 0; end--; }

        // Store in CATEGORIES
        strncpy(CATEGORIES[NUM_CATEGORIES].name, key,
                sizeof(CATEGORIES[NUM_CATEGORIES].name) - 1);

        if (strncmp(val, "local://", 8) == 0) {
            // Local path
            CATEGORIES[NUM_CATEGORIES].url[0] = 0;
            strncpy(CATEGORIES[NUM_CATEGORIES].localpath, val + 8,
                    sizeof(CATEGORIES[NUM_CATEGORIES].localpath) - 1);
        } else {
            // Remote URL
            strncpy(CATEGORIES[NUM_CATEGORIES].url, val,
                    sizeof(CATEGORIES[NUM_CATEGORIES].url) - 1);
            CATEGORIES[NUM_CATEGORIES].localpath[0] = 0;
        }

        NUM_CATEGORIES++;
    }
    fclose(f);
}

static size_t write_callback(char *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    MemoryBuffer *buf = (MemoryBuffer *)userp;
    buf->data = realloc(buf->data, buf->size + total + 1);
    if (!buf->data) return 0;
    memcpy(&(buf->data[buf->size]), contents, total);
    buf->size += total;
    buf->data[buf->size] = 0;
    return total;
}

SDL_Texture* fetch_image(SDL_Renderer *renderer, const char *url, char *status_out, size_t status_len) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(status_out, status_len, "curl_easy_init failed");
        return NULL;
    }

    MemoryBuffer buf = {0};
    buf.data = malloc(1);
    buf.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXPhotoFrame/" APP_VERSION " (Nintendo Switch)");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        snprintf(status_out, status_len, "Fetch error: %s", curl_easy_strerror(res));
        free(buf.data);
        return NULL;
    }

    if (buf.size == 0) {
        snprintf(status_out, status_len, "Empty response (HTTP %ld)", http_code);
        free(buf.data);
        return NULL;
    }

    SDL_RWops *rw = SDL_RWFromMem(buf.data, (int)buf.size);
    if (!rw) {
        snprintf(status_out, status_len, "SDL_RWFromMem failed");
        free(buf.data);
        return NULL;
    }

    SDL_Surface *surface = IMG_Load_RW(rw, 1);
    free(buf.data);

    if (!surface) {
        snprintf(status_out, status_len, "IMG_Load failed: %s", IMG_GetError());
        return NULL;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);

    if (!texture) {
        snprintf(status_out, status_len, "CreateTexture failed");
        return NULL;
    }

    snprintf(status_out, status_len, "OK (%zu bytes, HTTP %ld)", buf.size, http_code);
    return texture;
}

// Count and collect image files recursively
static int collect_images(const char *folderpath, char ***list, int *count, int *capacity) {
    DIR *dir = opendir(folderpath);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s%s", folderpath, entry->d_name);

        if (entry->d_type == DT_DIR) {
            // Recurse into subdirectory
            char subpath[512];
            snprintf(subpath, sizeof(subpath), "%s%s/", folderpath, entry->d_name);
            collect_images(subpath, list, count, capacity);
        } else {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (
                strcasecmp(ext, ".jpg") == 0 ||
                strcasecmp(ext, ".jpeg") == 0 ||
                strcasecmp(ext, ".png") == 0)) {
                // Grow list if needed
                if (*count >= *capacity) {
                    *capacity *= 2;
                    *list = realloc(*list, *capacity * sizeof(char *));
                }
                (*list)[*count] = strdup(fullpath);
                (*count)++;
            }
        }
    }
    closedir(dir);
    return *count;
}

SDL_Texture* load_local_image(SDL_Renderer *renderer, const char *folderpath,
                               char *status_out, size_t status_len) {
    // Check folder exists first
    DIR *test = opendir(folderpath);
    if (!test) {
        snprintf(status_out, status_len, "Folder not found: %s", folderpath);
        return NULL;
    }
    closedir(test);

    // Collect all images recursively
    int count = 0;
    int capacity = 64;
    char **imagelist = malloc(capacity * sizeof(char *));

    collect_images(folderpath, &imagelist, &count, &capacity);

    if (count == 0) {
        free(imagelist);
        snprintf(status_out, status_len, "No images found in %s", folderpath);
        return NULL;
    }

    // Pick a random one
    int target = rand() % count;
    char chosen[512];
    strncpy(chosen, imagelist[target], sizeof(chosen) - 1);

    // Free the list
    for (int i = 0; i < count; i++) free(imagelist[i]);
    free(imagelist);

    SDL_Surface *surface = IMG_Load(chosen);
    if (!surface) {
        snprintf(status_out, status_len, "IMG_Load failed: %s", IMG_GetError());
        return NULL;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);

    if (!texture) {
        snprintf(status_out, status_len, "CreateTexture failed");
        return NULL;
    }

    // Show just the filename in status, not the full path
    const char *filename = strrchr(chosen, '/');
    snprintf(status_out, status_len, "Local: %s", filename ? filename + 1 : chosen);
    return texture;
}

void render_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Color color, int x, int y) {
    SDL_Surface *s = TTF_RenderUTF8_Blended(font, text, color);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_Rect dst = {x, y, s->w, s->h};
    SDL_RenderCopy(renderer, t, NULL, &dst);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

void render_ui(SDL_Renderer *renderer, TTF_Font *font, int interval_mins,
               int cat_index, const char *fetch_status) {
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color yellow = {255, 220,  80, 255};
    SDL_Color cyan   = { 80, 220, 255, 255};

    // Background bar — taller to fit category row
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_Rect bar = {0, SCREEN_H - 100, SCREEN_W, 100};
    SDL_RenderFillRect(renderer, &bar);

    // Row 1: category selector
    int row1_y = SCREEN_H - 95;
    char cat_line[128];
    snprintf(cat_line, sizeof(cat_line), "%s %s  Category: [%s]  (%d/%d)",
             ICON_DLEFT, ICON_DRIGHT, CATEGORIES[cat_index].name, cat_index + 1, NUM_CATEGORIES);
    render_text(renderer, font, cat_line, cyan, 20, row1_y);

    // Row 2: interval + fetch status
    char line2[256];
    snprintf(line2, sizeof(line2),
             "Interval: %d min(s)     %s Increase     %s Decrease     %s Exit",
             interval_mins, ICON_PLUS, ICON_MINUS, ICON_B);
    render_text(renderer, font, line2, white, 20, SCREEN_H - 62);

    char line3[256];
    snprintf(line3, sizeof(line3), "Fetch: %s", fetch_status);
    render_text(renderer, font, line3, yellow, 20, SCREEN_H - 32);
}

int main(int argc, char *argv[]) {
    romfsInit();
	fsdevMountSdmc();
	load_config();
    socketInitializeDefault();
    appletInitialize();
	
	Uint32 last_charger_check = 0;
    PsmChargerType last_charger = PsmChargerType_Unconnected;
	
    // Initial charger state
    psmInitialize();
    psmGetChargerType(&last_charger);
    psmExit();
    if (last_charger != PsmChargerType_Unconnected) {
        appletSetMediaPlaybackState(true);
    }
	
	char fetch_status[256] = "Waiting...";
	
	// Check for internet connection at start.
    NifmInternetConnectionStatus netStatus = NifmInternetConnectionStatus_ConnectingUnknown1;
    nifmInitialize(NifmServiceType_User);
    Result rc = nifmGetInternetConnectionStatus(NULL, NULL, &netStatus);
    nifmExit();
    if (R_FAILED(rc) || netStatus != NifmInternetConnectionStatus_Connected) {
        snprintf(fetch_status, sizeof(fetch_status), "No internet connection.");
    }
	
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    TTF_Init();
    curl_global_init(CURL_GLOBAL_ALL);

    SDL_Window *window = SDL_CreateWindow("NX PhotoFrame",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    SDL_Joystick *joystick = SDL_JoystickOpen(0);
    TTF_Font *font = TTF_OpenFont("romfs:/font.ttf", 22);

    int interval_mins = DEFAULT_INTERVAL_MINS;
    int cat_index     = 0;
	int pending_fetch = 0;
    int ui_visible    = 1;
    Uint32 ui_show_time = SDL_GetTicks();
    Uint32 last_fetch   = SDL_GetTicks() - (interval_mins * 60 * 1000);
    SDL_Texture *current_image = NULL;

    while (1) {
        Uint32 now = SDL_GetTicks();

        // Fetch when timer expires
        if ((now - last_fetch) >= (Uint32)(interval_mins * 60 * 1000)) {
            SDL_Texture *new_image = NULL;

            if (CATEGORIES[cat_index].url[0] != 0) {
                // Remote fetch — check network first
                nifmInitialize(NifmServiceType_User);
                rc = nifmGetInternetConnectionStatus(NULL, NULL, &netStatus);
                nifmExit();

                if (R_SUCCEEDED(rc) && netStatus == NifmInternetConnectionStatus_Connected) {
                    // Show loading overlay
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                    SDL_RenderClear(renderer);
                    if (current_image) SDL_RenderCopy(renderer, current_image, NULL, NULL);
                    if (font) {
                        SDL_Color white = {255,255,255,255};
                        SDL_Surface *ls = TTF_RenderUTF8_Blended(font, "Loading...", white);
                        if (ls) {
                            SDL_Texture *lt = SDL_CreateTextureFromSurface(renderer, ls);
                            SDL_Rect dst = {(SCREEN_W-ls->w)/2, (SCREEN_H-ls->h)/2, ls->w, ls->h};
                            SDL_RenderCopy(renderer, lt, NULL, &dst);
                            SDL_DestroyTexture(lt);
                            SDL_FreeSurface(ls);
                        }
                    }
                    SDL_RenderPresent(renderer);
                    new_image = fetch_image(renderer,
                        CATEGORIES[cat_index].url, fetch_status, sizeof(fetch_status));
                } else {
                    snprintf(fetch_status, sizeof(fetch_status), "No internet connection.");
                }

            } else if (CATEGORIES[cat_index].localpath[0] != 0) {
                // Local fetch — no network check needed
                new_image = load_local_image(renderer,
                    CATEGORIES[cat_index].localpath, fetch_status, sizeof(fetch_status));
            }

            if (new_image) {
                if (current_image) SDL_DestroyTexture(current_image);
                current_image = new_image;
            }
            last_fetch = SDL_GetTicks();
            ui_visible = 1;
            ui_show_time = SDL_GetTicks();
        }

        // Re-check charger state every 30 seconds
        if (now - last_charger_check >= 30000) {
            last_charger_check = now;
            PsmChargerType current_charger = PsmChargerType_Unconnected;
            psmInitialize();
            psmGetChargerType(&current_charger);
            psmExit();
            if (current_charger != last_charger) {
                last_charger = current_charger;
                appletSetMediaPlaybackState(current_charger != PsmChargerType_Unconnected);
            }
        }

        // Render
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (current_image) {
            SDL_RenderCopy(renderer, current_image, NULL, NULL);
        } else if (font) {
            SDL_Color white = {255,255,255,255};
            SDL_Surface *s = TTF_RenderUTF8_Blended(font, fetch_status, white);
            if (s) {
                SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
                SDL_Rect dst = {(SCREEN_W-s->w)/2, (SCREEN_H-s->h)/2, s->w, s->h};
                SDL_RenderCopy(renderer, t, NULL, &dst);
                SDL_DestroyTexture(t);
                SDL_FreeSurface(s);
            }
        }
        if (ui_visible && font)
            render_ui(renderer, font, interval_mins, cat_index, fetch_status);
        SDL_RenderPresent(renderer);

        if (ui_visible && (now - ui_show_time > UI_HIDE_DELAY_MS)) {
            ui_visible = 0;
            if (pending_fetch) {
                pending_fetch = 0;
                last_fetch = now - (interval_mins * 60 * 1000); // trigger immediate fetch
            }
        }

        // Events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    goto cleanup;

                case SDL_FINGERDOWN:
                case SDL_MOUSEBUTTONDOWN:
                    ui_visible = 1;
                    ui_show_time = SDL_GetTicks();
                    break;

                case SDL_JOYBUTTONDOWN:
                    switch (event.jbutton.button) {
                        case BTN_B:
                            goto cleanup;
                        case BTN_PLUS:
                            if (interval_mins < 1440) interval_mins++;
                            ui_visible = 1;
                            ui_show_time = SDL_GetTicks();
                            break;
                        case BTN_MINUS:
                            if (interval_mins > 5) interval_mins--;
                            ui_visible = 1;
                            ui_show_time = SDL_GetTicks();
                            break;
                        case BTN_DLEFT:
                            cat_index = (cat_index - 1 + NUM_CATEGORIES) % NUM_CATEGORIES;
                            // Force immediate fetch of new category
                            pending_fetch = 1;
                            ui_visible = 1;
                            ui_show_time = SDL_GetTicks();
                            break;
                        case BTN_DRIGHT:
                            cat_index = (cat_index + 1) % NUM_CATEGORIES;
                            pending_fetch = 1;
                            ui_visible = 1;
                            ui_show_time = SDL_GetTicks();
                            break;
                        default:
                            ui_visible = 1;
                            ui_show_time = SDL_GetTicks();
                            break;
                    }
                    break;
            }
        }

        SDL_Delay(16);
    }

cleanup:
    if (current_image) SDL_DestroyTexture(current_image);
    if (font) TTF_CloseFont(font);
    if (joystick) SDL_JoystickClose(joystick);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    curl_global_cleanup();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    appletSetMediaPlaybackState(false);
    appletExit();
    socketExit();
	fsdevUnmountDevice("sdmc");
    romfsExit();
    return 0;
}