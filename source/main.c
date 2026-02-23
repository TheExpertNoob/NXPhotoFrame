#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <curl/curl.h>

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

typedef struct {
    const char *name;
    const char *url;
} Category;

static const Category CATEGORIES[] = {
    { "Video Games",    "https://gandalfsax.com/images/vg.jpg"    },
	{ "Halloween",      "https://gandalfsax.com/images/hw.jpg"    },
    { "Ancient Girls",  "https://gandalfsax.com/images/ag.jpg"    },
    { "Gaming Girls",   "https://gandalfsax.com/images/gg.jpg"    },
    { "Lofi Time",      "https://gandalfsax.com/images/lt.jpg"    },
    { "Waifu & Chill",  "https://gandalfsax.com/images/wac.jpg"   },
	{ "All Girls",      "https://gandalfsax.com/images/girls.jpg" },
};
#define NUM_CATEGORIES 7

typedef struct {
    unsigned char *data;
    size_t size;
} MemoryBuffer;

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

void render_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Color color, int x, int y) {
    SDL_Surface *s = TTF_RenderText_Blended(font, text, color);
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
    snprintf(cat_line, sizeof(cat_line), "[<] [>]  Category:  [ %s ]  (%d/%d)",
             CATEGORIES[cat_index].name, cat_index + 1, NUM_CATEGORIES);
    render_text(renderer, font, cat_line, cyan, 20, row1_y);

    // Row 2: interval + fetch status
    char line2[256];
    snprintf(line2, sizeof(line2),
             "Interval: %d min(s)   [+] Increase  [-] Decrease  [B] Exit",
             interval_mins);
    render_text(renderer, font, line2, white, 20, SCREEN_H - 62);

    char line3[256];
    snprintf(line3, sizeof(line3), "Fetch: %s", fetch_status);
    render_text(renderer, font, line3, yellow, 20, SCREEN_H - 32);
}

int main(int argc, char *argv[]) {
    romfsInit();
    socketInitializeDefault();
    appletInitialize();
	
	// Check if switch is docked or charging then disable screen dimming/sleep.
    psmInitialize();
    PsmChargerType charger = PsmChargerType_Unconnected;
    psmGetChargerType(&charger);
    psmExit();
    if (charger != PsmChargerType_Unconnected) {
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
    int ui_visible    = 1;
    Uint32 ui_show_time = SDL_GetTicks();
    Uint32 last_fetch   = SDL_GetTicks() - (interval_mins * 60 * 1000);
    SDL_Texture *current_image = NULL;

    while (1) {
        Uint32 now = SDL_GetTicks();

        if (ui_visible && (now - ui_show_time > UI_HIDE_DELAY_MS))
            ui_visible = 0;

        // Fetch when timer expires
        if ((now - last_fetch) >= (Uint32)(interval_mins * 60 * 1000)) {
            // Re-check network before fetching
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
                    SDL_Surface *ls = TTF_RenderText_Blended(font, "Loading...", white);
                    if (ls) {
                        SDL_Texture *lt = SDL_CreateTextureFromSurface(renderer, ls);
                        SDL_Rect dst = {(SCREEN_W-ls->w)/2, (SCREEN_H-ls->h)/2, ls->w, ls->h};
                        SDL_RenderCopy(renderer, lt, NULL, &dst);
                        SDL_DestroyTexture(lt);
                        SDL_FreeSurface(ls);
                    }
                }
                SDL_RenderPresent(renderer);

                SDL_Texture *new_image = fetch_image(renderer,
                    CATEGORIES[cat_index].url, fetch_status, sizeof(fetch_status));
                if (new_image) {
                    if (current_image) SDL_DestroyTexture(current_image);
                    current_image = new_image;
                }
            } else {
                snprintf(fetch_status, sizeof(fetch_status), "No internet connection.");
            }
            last_fetch = SDL_GetTicks();
            ui_visible = 1;
            ui_show_time = SDL_GetTicks();
        }

        // Render
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (current_image) {
            SDL_RenderCopy(renderer, current_image, NULL, NULL);
        } else if (font) {
            SDL_Color white = {255,255,255,255};
            SDL_Surface *s = TTF_RenderText_Blended(font, fetch_status, white);
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
                            interval_mins++;
                            ui_visible = 1;
                            ui_show_time = SDL_GetTicks();
                            break;
                        case BTN_MINUS:
                            if (interval_mins > 1) interval_mins--;
                            ui_visible = 1;
                            ui_show_time = SDL_GetTicks();
                            break;
                        case BTN_DLEFT:
                            cat_index = (cat_index - 1 + NUM_CATEGORIES) % NUM_CATEGORIES;
                            // Force immediate fetch of new category
                            last_fetch = SDL_GetTicks() - (interval_mins * 60 * 1000);
                            ui_visible = 1;
                            ui_show_time = SDL_GetTicks();
                            break;
                        case BTN_DRIGHT:
                            cat_index = (cat_index + 1) % NUM_CATEGORIES;
                            last_fetch = SDL_GetTicks() - (interval_mins * 60 * 1000);
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
    romfsExit();
    return 0;
}