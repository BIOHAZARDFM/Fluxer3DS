
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <malloc.h>
#include <sys/stat.h>
#include <dirent.h>
#include <curl/curl.h>
#include "altcha.h"

static u32 theme_color(u8 r, u8 g, u8 b, u8 a);
static int ui_dark_mode = 0;
static u8 theme_primary[3] = {119,202,225};
static u8 theme_secondary[3] = {35,151,190};
static int theme_edit_target = 0;
static int theme_wheel_open = 0;

#define API_BASE       "https://api.fluxer.app/v1"
#define SOC_ALIGN      0x1000
#define SOC_BUFFERSIZE 0x100000
#define HTTP_BODY_MAX  262144
#define MESSAGE_BODY_MAX 262144
#define TOKEN_MAX      2048
#define INPUT_MAX      384

#define COL_BG       theme_color(119, 202, 225, 255)
#define COL_PANEL    theme_color(225, 247, 251, 238)
#define COL_PANEL2   theme_color(176, 226, 239, 255)
#define COL_ACCENT   theme_color(35, 151, 190, 255)
#define COL_TEXT     theme_color(24, 65, 83, 255)
#define COL_MUTED    theme_color(72, 119, 137, 255)
#define COL_GREEN    theme_color(44, 151, 115, 255)
#define COL_RED      theme_color(190, 75, 75, 255)

static u32 *soc_buffer = NULL;
static int network_ready = 0;
static int sslc_ready = 0;

static char session_token[TOKEN_MAX];
static int session_loaded_from_disk = 0;
static char account_name[96] = "Not signed in";
static char status_text[256] =
    "Connect to Wi-Fi, then sign in.";

static char mfa_ticket[256];
static char pending_login_body[INPUT_MAX * 4 + 128];
static char pending_captcha_token[4096];
static char captcha_retry_response[HTTP_BODY_MAX];

static int app_state = 0;
/*
    0 = Login screen
    1 = MFA screen
    2 = Signed in
    3 = Error screen
*/

static C2D_Font ui_font;
static C2D_TextBuf text_buffer;


static u32 theme_color(u8 r, u8 g, u8 b, u8 a)
{
    int rr=r, gg=g, bb=b;
    if(r==119 && g==202 && b==225) { rr=theme_primary[0]; gg=theme_primary[1]; bb=theme_primary[2]; }
    else if(r==35 && g==151 && b==190) { rr=theme_secondary[0]; gg=theme_secondary[1]; bb=theme_secondary[2]; }
    else if (g > r && b > r && b > 100) {
        int brightness=(r*30+g*59+b*11)/100;
        int t=brightness;
        if(t<55)t=55;
        if(t>235)t=235;
        t=(t-55)*255/180;
        rr=(theme_secondary[0]*(255-t)+theme_primary[0]*t)/255;
        gg=(theme_secondary[1]*(255-t)+theme_primary[1]*t)/255;
        bb=(theme_secondary[2]*(255-t)+theme_primary[2]*t)/255;
    }
    if (ui_dark_mode) {
        int luminance=(rr*30+gg*59+bb*11)/100;
        if(luminance>=190) rr=gg=bb=58;
        else if(luminance>=105) rr=gg=bb=82;
        else rr=gg=bb=222;
    } else if(r>180&&g>180&&b>180) {
        rr=gg=bb=250;
    }
    return C2D_Color32((u8)rr,(u8)gg,(u8)bb,a);
}

static void save_ui_theme(void)
{
    FILE *f;
    mkdir("sdmc:/3ds",0777);
    mkdir("sdmc:/3ds/Fluxer3DS",0777);
    f=fopen("sdmc:/3ds/Fluxer3DS/theme.dat","wb");
    if(!f)return;
    fwrite(theme_primary,1,3,f);
    fwrite(theme_secondary,1,3,f);
    fclose(f);
}
static void load_ui_theme(void)
{
    FILE *f=fopen("sdmc:/3ds/Fluxer3DS/theme.dat","rb");
    u8 colors[6];
    if(!f)return;
    if(fread(colors,1,6,f)==6) {
        memcpy(theme_primary,colors,3);
        memcpy(theme_secondary,colors+3,3);
    }
    fclose(f);
}
static void hsv_to_rgb(float h,float s,u8 *r,u8 *g,u8 *b)
{
    float c=s, x=c*(1.0f-fabsf(fmodf(h/60.0f,2.0f)-1.0f));
    float rr=0,gg=0,bb=0;
    if(h<60){rr=c;gg=x;} else if(h<120){rr=x;gg=c;}
    else if(h<180){gg=c;bb=x;} else if(h<240){gg=x;bb=c;}
    else if(h<300){rr=x;bb=c;} else {rr=c;bb=x;}
    *r=(u8)(rr*255.0f); *g=(u8)(gg*255.0f); *b=(u8)(bb*255.0f);
}
static void draw_theme_color_wheel(void)
{
    int ring,step;
    const float cx=96.0f,cy=174.0f;
    for(ring=1;ring<=8;ring++) {
        float sat=(float)ring/8.0f;
        float radius=7.0f+(float)ring*7.0f;
        for(step=0;step<72;step++) {
            float angle=(float)step*5.0f;
            float rad=angle*0.0174532925f;
            u8 r,g,b;
            hsv_to_rgb(angle,sat,&r,&g,&b);
            C2D_DrawCircleSolid(cx+cosf(rad)*radius,cy+sinf(rad)*radius,0.0f,4.2f,C2D_Color32(r,g,b,255));
        }
    }
}
static void choose_theme_color(int x,int y)
{
    float dx=(float)x-96.0f,dy=(float)y-174.0f;
    float radius=sqrtf(dx*dx+dy*dy);
    float angle=atan2f(dy,dx)*57.2957795f;
    u8 *target=theme_edit_target?theme_secondary:theme_primary;
    if(angle<0)angle+=360.0f;
    if(radius>64.0f)radius=64.0f;
    hsv_to_rgb(angle,radius/64.0f,&target[0],&target[1],&target[2]);
    save_ui_theme();
}


/* ---------------------------------------------------------
   GENERAL HELPERS
   --------------------------------------------------------- */

static void set_error(char *detail, size_t size,
                      const char *stage, Result rc)
{
    if (detail && size > 0) {
        snprintf(detail, size, "%s: 0x%08lX",
                 stage, (unsigned long)rc);
    }
}

static int hex_digit_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static unsigned int read_json_u16(const char *p)
{
    unsigned int value = 0;
    int i;
    for (i = 0; i < 4; i++) {
        int h = hex_digit_value(p[i]);
        if (h < 0) return 0xFFFFFFFFu;
        value = (value << 4) | (unsigned int)h;
    }
    return value;
}

static void append_utf8_codepoint(char *out, size_t cap, size_t *used,
                                  unsigned int cp)
{
    unsigned char bytes[4];
    size_t count, i;
    if (cp <= 0x7F) { bytes[0] = (unsigned char)cp; count = 1; }
    else if (cp <= 0x7FF) {
        bytes[0] = (unsigned char)(0xC0 | (cp >> 6));
        bytes[1] = (unsigned char)(0x80 | (cp & 0x3F)); count = 2;
    } else if (cp <= 0xFFFF) {
        bytes[0] = (unsigned char)(0xE0 | (cp >> 12));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | (cp & 0x3F)); count = 3;
    } else if (cp <= 0x10FFFF) {
        bytes[0] = (unsigned char)(0xF0 | (cp >> 18));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[3] = (unsigned char)(0x80 | (cp & 0x3F)); count = 4;
    } else return;
    if (*used + count >= cap) return;
    for (i = 0; i < count; i++) out[(*used)++] = (char)bytes[i];
}


static int json_string(const char *json,const char *key,char *out,size_t out_size);
/* Read only direct members of a JSON object. The older strstr-based helper
   could accidentally pick "content"/"id" from nested embeds or attachments. */
static const char *json_value_end(const char *p) {
    int depth=0, quoted=0, escaped=0;
    if(!p) return NULL;
    while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
    if(*p=='"') {
        p++;
        while(*p) {
            if(escaped) escaped=0;
            else if(*p=='\\') escaped=1;
            else if(*p=='"') return p+1;
            p++;
        }
        return NULL;
    }
    if(*p=='{'||*p=='[') {
        char open=*p, close=(open=='{'?'}':']');
        depth=1; p++;
        while(*p&&depth) {
            if(quoted) {
                if(escaped) escaped=0;
                else if(*p=='\\') escaped=1;
                else if(*p=='"') quoted=0;
            } else {
                if(*p=='"') quoted=1;
                else if(*p==open) depth++;
                else if(*p==close) depth--;
            }
            p++;
        }
        return depth==0?p:NULL;
    }
    while(*p&&*p!=','&&*p!='}'&&*p!=']') p++;
    return p;
}
static int json_member_span(const char *obj,const char *key,
                            const char **value_start,const char **value_end) {
    const char *p=obj;
    size_t key_len;
    if(!p||!key||*p!='{') return 0;
    key_len=strlen(key); p++;
    for(;;) {
        const char *ks,*ke,*vs,*ve;
        while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n'||*p==',') p++;
        if(*p=='}'||!*p) return 0;
        if(*p!='"') return 0;
        ks=++p;
        while(*p&&(*p!='"'||(p>ks&&p[-1]=='\\'))) p++;
        if(!*p) return 0;
        ke=p++;
        while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
        if(*p++!=':') return 0;
        while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
        vs=p; ve=json_value_end(vs); if(!ve) return 0;
        if((size_t)(ke-ks)==key_len&&memcmp(ks,key,key_len)==0) {
            if(value_start) *value_start=vs;
            if(value_end) *value_end=ve;
            return 1;
        }
        p=ve;
        while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
        if(*p=='}'||!*p) return 0;
        if(*p!=',') return 0;
        p++;
    }
}
static int json_top_string(const char *obj,const char *key,char *out,size_t cap) {
    const char *vs,*ve;
    char wrapped[4096];
    size_t n;
    if(!json_member_span(obj,key,&vs,&ve)||*vs!='"') { if(cap) out[0]='\0'; return 0; }
    n=(size_t)(ve-vs);
    if(n+8>=sizeof(wrapped)) { if(cap) out[0]='\0'; return 0; }
    memcpy(wrapped,"{\"v\":",5); memcpy(wrapped+5,vs,n);
    wrapped[5+n]='}'; wrapped[6+n]='\0';
    return json_string(wrapped,"v",out,cap);
}
static int json_top_object(const char *obj,const char *key,char *out,size_t cap) {
    const char *vs,*ve; size_t n;
    if(!json_member_span(obj,key,&vs,&ve)||*vs!='{') { if(cap) out[0]='\0'; return 0; }
    n=(size_t)(ve-vs); if(n>=cap) { if(cap) out[0]='\0'; return 0; }
    memcpy(out,vs,n); out[n]='\0'; return 1;
}

static int json_string(const char *json, const char *key,
                       char *out, size_t out_size)
{
    char needle[80];
    const char *p;
    size_t n = 0;
    int truncated = 0;

    if (!json || !key || !out || out_size < 2) return 0;
    out[0] = '\0';
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) return 0;
    p = strchr(p + strlen(needle), ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0;
    p++;

    while (*p && *p != '"') {
        unsigned int cp;
        if (*p == '\\') {
            p++;
            if (!*p) break;
            if (*p == 'u') {
                unsigned int first;
                p++;
                if (strlen(p) < 4 || (first = read_json_u16(p)) == 0xFFFFFFFFu) {
                    /* Ignore malformed/unsupported escape sequences. */
                    p += strlen(p);
                    break;
                }
                p += 4;
                cp = first;
                if (first >= 0xD800 && first <= 0xDBFF &&
                    p[0] == '\\' && p[1] == 'u' && strlen(p + 2) >= 4) {
                    unsigned int second = read_json_u16(p + 2);
                    if (second >= 0xDC00 && second <= 0xDFFF) {
                        cp = 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
                        p += 6;
                    } else cp = '?';
                } else if (first >= 0xD800 && first <= 0xDFFF) {
                    /* Unsupported surrogate: omit it rather than showing '?'. */
                    continue;
                }
                if (!truncated) {
                    size_t before = n;
                    append_utf8_codepoint(out, out_size, &n, cp);
                    if (n == before) truncated = 1;
                }
                continue;
            }
            switch (*p) {
                case '"': cp = '"'; break;
                case '\\': cp = '\\'; break;
                case '/': cp = '/'; break;
                case 'b': cp = '\b'; break;
                case 'f': cp = '\f'; break;
                case 'n': cp = '\n'; break;
                case 'r': cp = '\r'; break;
                case 't': cp = '\t'; break;
                default: cp = (unsigned char)*p; break;
            }
            p++;
        } else {
            cp = (unsigned char)*p++;
        }
        if (!truncated) {
            size_t before = n;
            append_utf8_codepoint(out, out_size, &n, cp);
            if (n == before) truncated = 1;
        }
    }
    out[n] = '\0';
    return (*p == '"');
}

static int json_escape(const char *input,
                       char *output, size_t output_size)
{
    size_t n = 0;

    if (!input || !output || output_size < 3)
        return 0;

    while (*input) {
        unsigned char c = (unsigned char)*input++;

        if (c == '"' || c == '\\') {
            if (n + 2 >= output_size)
                return 0;

            output[n++] = '\\';
            output[n++] = (char)c;
        }
        else if (c < 32) {
            if (n + 1 >= output_size)
                return 0;

            output[n++] = ' ';
        }
        else {
            if (n + 1 >= output_size)
                return 0;

            output[n++] = (char)c;
        }
    }

    output[n] = '\0';
    return 1;
}


static Result api_request(HTTPC_RequestMethod method,const char *path,const char *body,const char *auth_token,const char *captcha_token,char *response,size_t response_size,u32 *http_status,char *detail,size_t detail_size);
static int keyboard_input(const char *hint,char *output,size_t output_size,int password_mode);
static void load_home_data(void);
static int save_session_token(void);
static int load_session_token(void);
static void delete_saved_session(void);

/* ---------------- Fluxer chat browser ---------------- */
#define MAX_ITEMS 256
#define MAX_MESSAGES 50
typedef struct { char id[32]; char name[72]; char peer_id[32]; int type; } ChatItem;
typedef struct { char id[32]; char author[48]; char content[512]; } ChatMessage;
typedef struct { char id[32]; char name[72]; int dm_index; } FriendItem;
static ChatItem dm_items[MAX_ITEMS], guild_items[MAX_ITEMS], channel_items[MAX_ITEMS];
static FriendItem friend_items[MAX_ITEMS];
static ChatMessage message_items[MAX_MESSAGES];
static int dm_count=0,guild_count=0,channel_count=0,message_count=0,friend_count=0;
static int circle_scroll_latch=0;
static int screen_state=0,selected_index=0,list_offset=0,active_guild=0,active_channel=0,active_is_dm=0;
static int chat_return_screen=0,chat_return_selection=0;
static int settings_return_screen=0;
static int transition_active=0,pending_guild_index=-1;
static u64 transition_started=0;
static int emoji_panel_open=0;
static int login_unzip_active=0;
static u64 login_unzip_started=0;
static char message_draft[INPUT_MAX]="";
static char home_status[160]="Loading Fluxer chats...",selected_channel_name[72]="";

static const char *object_end(const char *p) {
    int depth=0, quoted=0, escaped=0;
    for(;*p;p++) {
        if(quoted) { if(escaped) escaped=0; else if(*p=='\\') escaped=1; else if(*p=='"') quoted=0; }
        else { if(*p=='"') quoted=1; else if(*p=='{') depth++; else if(*p=='}') { depth--; if(depth==0) return p+1; } }
    }
    return NULL;
}
static int json_int_value(const char *json,const char *key,int fallback) {
    char needle[80]; const char *p;
    snprintf(needle,sizeof(needle),"\"%s\"",key);
    p=strstr(json,needle); if(!p) return fallback;
    p=strchr(p+strlen(needle),':'); return p?atoi(p+1):fallback;
}
static int parse_items(const char *json,ChatItem *out,int max,int dm) {
    const char *p=strchr(json,'['); int count=0;
    if (!p)
        return 0;
    p++;
    while(*p && count<max) {
        if(*p=='{') {
            const char *end=object_end(p); char obj[4096]; size_t n;
            if (!end)
                break;
            n = (size_t)(end - p);
            if (n >= sizeof(obj))
                n = sizeof(obj) - 1;
            memcpy(obj,p,n); obj[n]='\0'; memset(&out[count],0,sizeof(out[count]));
            json_string(obj,"id",out[count].id,sizeof(out[count].id));
            json_string(obj,"name",out[count].name,sizeof(out[count].name));
            out[count].type=json_int_value(obj,"type",0);
            if(dm) {
                const char *r=strstr(obj,"\"recipients\"");
                char display_name[72]={0};
                if(r) {
                    const char *person=strchr(r,'{');
                    const char *person_end=person?object_end(person):NULL;
                    if(person&&person_end) {
                        char person_obj[1024]; size_t pn=(size_t)(person_end-person);
                        if(pn>=sizeof(person_obj)) pn=sizeof(person_obj)-1;
                        memcpy(person_obj,person,pn); person_obj[pn]='\0';
                        json_string(person_obj,"id",out[count].peer_id,sizeof(out[count].peer_id));
                        if(!json_string(person_obj,"global_name",display_name,sizeof(display_name)))
                            json_string(person_obj,"username",display_name,sizeof(display_name));
                    }
                }
                if(display_name[0]) snprintf(out[count].name,sizeof(out[count].name),"%s",display_name);
                if(!out[count].name[0]) snprintf(out[count].name,sizeof(out[count].name),"Direct message");
            }
            if(out[count].id[0]) count++;
            p=end;
        } else p++;
    }
    return count;
}
static void load_friends(void) {
    static char response[HTTP_BODY_MAX];
    char detail[128]; u32 code=0; const char *p;
    friend_count=0;
    Result rc=api_request(HTTPC_METHOD_GET,"/users/@me/relationships",NULL,session_token,NULL,response,sizeof(response),&code,detail,sizeof(detail));
    if(R_FAILED(rc)||code!=200) return;
    p=strchr(response,'['); if(!p) return; p++;
    while(*p&&friend_count<MAX_ITEMS) {
        if(*p=='{') {
            const char *end=object_end(p); char obj[4096]; size_t n;
            if(!end) break;
            n=(size_t)(end-p); if(n>=sizeof(obj)) n=sizeof(obj)-1;
            memcpy(obj,p,n); obj[n]='\0';
            if(json_int_value(obj,"type",0)==1) {
                const char *u=strstr(obj,"\"user\""); const char *ue=u?strchr(u,'{'):NULL; const char *uend=ue?object_end(ue):NULL;
                FriendItem *fr=&friend_items[friend_count]; memset(fr,0,sizeof(*fr)); fr->dm_index=-1;
                if(ue&&uend) {
                    char user_obj[1536]; size_t un=(size_t)(uend-ue);
                    if(un>=sizeof(user_obj)) un=sizeof(user_obj)-1;
                    memcpy(user_obj,ue,un); user_obj[un]='\0';
                    /* Match DMs with the nested user ID, not the relationship ID. */
                    json_string(user_obj,"id",fr->id,sizeof(fr->id));
                    if(!json_string(user_obj,"global_name",fr->name,sizeof(fr->name)))
                        json_string(user_obj,"username",fr->name,sizeof(fr->name));
                }
                if(fr->id[0]) {
                    int j; for(j=0;j<dm_count;j++) if(strcmp(dm_items[j].peer_id,fr->id)==0) { fr->dm_index=j; break; }
                    if(!fr->name[0]) snprintf(fr->name,sizeof(fr->name),"Friend");
                    friend_count++;
                }
            }
            p=end;
        } else p++;
    }
}
static void load_home_data(void) {
    static char response[HTTP_BODY_MAX]; char detail[128]; u32 code=0;
    Result rc=api_request(HTTPC_METHOD_GET,"/users/@me/channels",NULL,session_token,NULL,response,sizeof(response),&code,detail,sizeof(detail));
    if(R_FAILED(rc)||code!=200) { snprintf(home_status,sizeof(home_status),"DM list failed: HTTP %lu",(unsigned long)code); return; }
    dm_count=parse_items(response,dm_items,MAX_ITEMS,1);
    rc=api_request(HTTPC_METHOD_GET,"/users/@me/guilds",NULL,session_token,NULL,response,sizeof(response),&code,detail,sizeof(detail));
    if(R_FAILED(rc)||code!=200) { snprintf(home_status,sizeof(home_status),"Loaded DMs; servers failed: HTTP %lu",(unsigned long)code); return; }
    guild_count=parse_items(response,guild_items,MAX_ITEMS,0);
    load_friends();
    snprintf(home_status,sizeof(home_status),"%d friends and %d communities loaded",friend_count,guild_count);
    screen_state=0; selected_index=0; list_offset=0;
}
static void load_guild_channels(int index) {
    static char response[HTTP_BODY_MAX]; char path[128],detail[128]; u32 code=0; int i,n=0;
    if(index<0||index>=guild_count) return;
    snprintf(path,sizeof(path),"/guilds/%s/channels",guild_items[index].id);
    Result rc=api_request(HTTPC_METHOD_GET,path,NULL,session_token,NULL,response,sizeof(response),&code,detail,sizeof(detail));
    if(R_FAILED(rc)||code!=200) {
        snprintf(home_status,sizeof(home_status),"Channels failed: HTTP %lu %.70s",(unsigned long)code,detail);
        return;
    }
    channel_count=parse_items(response,channel_items,MAX_ITEMS,0);
    {
        int raw_count=channel_count;
        for(i=0;i<raw_count;i++) {
            int type=channel_items[i].type;
            /* Keep categories, text, voice, announcement, forum and media entries visible. */
            if(type==0||type==2||type==4||type==5||type==15||type==16)
                channel_items[n++]=channel_items[i];
        }
        channel_count=n;
        active_guild=index;
        screen_state=1;
        selected_index=0;
        list_offset=0;
        if(channel_count==0)
            snprintf(home_status,sizeof(home_status),"No supported channels (API returned %d)",raw_count);
        else
            snprintf(home_status,sizeof(home_status),"%d channels and categories",channel_count);
    }
}
/* Sort decimal snowflake IDs oldest first so chat reads top-to-bottom. */
static void sort_messages_oldest_first(void) {
    int i,j;
    for(i=1;i<message_count;i++) {
        ChatMessage item=message_items[i];
        j=i-1;
        while(j>=0 && item.id[0] && message_items[j].id[0] &&
              (strlen(message_items[j].id)<strlen(item.id) ||
               (strlen(message_items[j].id)==strlen(item.id) &&
                strcmp(message_items[j].id,item.id)>0))) {
            message_items[j+1]=message_items[j];
            j--;
        }
        message_items[j+1]=item;
    }
}

static void load_messages(const char *channel_id,const char *name,int is_dm,int index) {
    static char response[MESSAGE_BODY_MAX]; char path[128],detail[128]; u32 code=0; const char *p;
    snprintf(path,sizeof(path),"/channels/%s/messages?limit=50",channel_id);
    Result rc=api_request(HTTPC_METHOD_GET,path,NULL,session_token,NULL,response,sizeof(response),&code,detail,sizeof(detail));
    if(R_FAILED(rc)||code!=200) { snprintf(home_status,sizeof(home_status),"Messages failed: HTTP %lu %.90s",(unsigned long)code,detail); return; }
    message_count=0; p=strchr(response,'['); if(p) p++;
    while(p&&*p&&message_count<MAX_MESSAGES) {
        if(*p=='{') {
            const char *end=object_end(p); char obj[4096]; size_t n;
            if (!end)
                break;
            n = (size_t)(end - p);
            if (n >= sizeof(obj)) { p=end; continue; }
            memcpy(obj,p,n); obj[n]='\0';
            memset(&message_items[message_count],0,sizeof(ChatMessage));
            json_top_string(obj,"id",message_items[message_count].id,sizeof(message_items[message_count].id));
            json_top_string(obj,"content",message_items[message_count].content,sizeof(message_items[message_count].content));
            {
                char author_obj[1536];
                if(json_top_object(obj,"author",author_obj,sizeof(author_obj))) {
                    if(!json_top_string(author_obj,"global_name",message_items[message_count].author,sizeof(message_items[message_count].author)))
                        json_top_string(author_obj,"username",message_items[message_count].author,sizeof(message_items[message_count].author));
                }
            }
            if(!message_items[message_count].id[0]) { p=end; continue; }
            if(!message_items[message_count].author[0]) snprintf(message_items[message_count].author,sizeof(message_items[message_count].author),"User");
            message_count++; p=end;
        } else p++;
    }
    sort_messages_oldest_first();
    chat_return_screen=screen_state; chat_return_selection=selected_index;
    active_channel=index; active_is_dm=is_dm; screen_state=2; selected_index=0; list_offset=message_count>3?message_count-3:0; emoji_panel_open=0;
    snprintf(selected_channel_name,sizeof(selected_channel_name),"%s",name);
    snprintf(home_status,sizeof(home_status),"%d recent messages",message_count);
}
static void open_message_keyboard(void) {
    char typed[INPUT_MAX];
    if(!keyboard_input("Write a message",typed,sizeof(typed),0)) return;
    snprintf(message_draft,sizeof(message_draft),"%s",typed);
    memset(typed,0,sizeof(typed));
}
static void append_emoticon(const char *value) {
    size_t used=strlen(message_draft), add=strlen(value);
    if(used+add+1<sizeof(message_draft)) memcpy(message_draft+used,value,add+1);
}
static void send_chat_message(void) {
    /* Keep request storage off the small 3DS application stack. */
    static char escaped[INPUT_MAX*2];
    static char body[INPUT_MAX*2+64];
    static char path[128];
    static char detail[128];
    static char response[16384];
    static char sent_content[INPUT_MAX];
    static char channel_id[32];
    u32 code=0;
    Result rc;
    int item_count=active_is_dm?dm_count:channel_count;
    if(active_channel<0||active_channel>=item_count) {
        snprintf(home_status,sizeof(home_status),"No channel is selected.");
        return;
    }
    {
        ChatItem *item=active_is_dm?&dm_items[active_channel]:&channel_items[active_channel];
        if(!item->id[0]) {
            snprintf(home_status,sizeof(home_status),"This channel has no valid ID.");
            return;
        }
        snprintf(channel_id,sizeof(channel_id),"%s",item->id);
    }
    if(!message_draft[0]) {
        snprintf(home_status,sizeof(home_status),"Write a message first.");
        return;
    }
    snprintf(sent_content,sizeof(sent_content),"%s",message_draft);
    if(!json_escape(sent_content,escaped,sizeof(escaped))) {
        snprintf(home_status,sizeof(home_status),"Message could not be encoded.");
        return;
    }
    snprintf(body,sizeof(body),"{\"content\":\"%s\"}",escaped);
    snprintf(path,sizeof(path),"/channels/%s/messages",channel_id);
    response[0]='\\0';
    detail[0]='\\0';
    rc=api_request(HTTPC_METHOD_POST,path,body,session_token,NULL,response,sizeof(response),&code,detail,sizeof(detail));
    memset(escaped,0,sizeof(escaped));
    memset(body,0,sizeof(body));
    if(R_FAILED(rc)||(code!=200&&code!=201)) {
        snprintf(home_status,sizeof(home_status),"Send failed: HTTP %lu",(unsigned long)code);
        if(detail[0]) snprintf(home_status,sizeof(home_status),"Send failed: %.110s",detail);
        memset(sent_content,0,sizeof(sent_content));
        return;
    }

    /* Do not issue a second HTTPS request inside the send action.
       Add the confirmed message locally; reopening the channel refreshes history. */
    if(message_count>=MAX_MESSAGES) {
        memmove(&message_items[0],&message_items[1],
                sizeof(ChatMessage)*(MAX_MESSAGES-1));
        message_count=MAX_MESSAGES-1;
    }
    {
        ChatMessage sent;
        memset(&sent,0,sizeof(sent));
        json_string(response,"id",sent.id,sizeof(sent.id));
        snprintf(sent.author,sizeof(sent.author),"%s",
                 account_name[0]?account_name:"You");
        snprintf(sent.content,sizeof(sent.content),"%s",sent_content);
        if(message_count>0)
            memmove(&message_items[1],&message_items[0],
                    sizeof(ChatMessage)*(message_count<MAX_MESSAGES?message_count:MAX_MESSAGES-1));
        message_items[0]=sent;
        if(message_count<MAX_MESSAGES) message_count++;
        sort_messages_oldest_first();
    }
    memset(message_draft,0,sizeof(message_draft));
    memset(sent_content,0,sizeof(sent_content));
    list_offset=message_count>3?message_count-3:0;
    snprintf(home_status,sizeof(home_status),"Message sent.");
}

/* ---------------------------------------------------------
   NETWORK - libcurl + mbedTLS
   --------------------------------------------------------- */

typedef struct {
    char *data;
    size_t capacity;
    size_t used;
} ResponseBuffer;

static size_t fluxer_curl_write_callback(
    char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ResponseBuffer *buffer = (ResponseBuffer *)userdata;
    size_t bytes;

    if (!buffer || !buffer->data || size == 0 ||
        nmemb > ((size_t)-1) / size)
        return 0;

    bytes = size * nmemb;

    if (bytes > buffer->capacity - buffer->used - 1)
        return 0;

    memcpy(buffer->data + buffer->used, ptr, bytes);
    buffer->used += bytes;
    buffer->data[buffer->used] = '\0';

    return bytes;
}

static int append_curl_header(
    struct curl_slist **headers, const char *value)
{
    struct curl_slist *next;

    next = curl_slist_append(*headers, value);
    if (!next)
        return 0;

    *headers = next;
    return 1;
}

static Result init_network(void)
{
    Result rc;

    soc_buffer = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);

    if (!soc_buffer)
        return -1;

    rc = socInit(soc_buffer, SOC_BUFFERSIZE);

    if (R_FAILED(rc)) {
        free(soc_buffer);
        soc_buffer = NULL;
        return rc;
    }

    /*
       mbedTLS in devkitPro's 3DS port obtains secure random bytes
       through the 3DS SSL service. Start it before any TLS request.
    */
    rc = sslcInit(0);
    if (R_FAILED(rc)) {
        socExit();
        free(soc_buffer);
        soc_buffer = NULL;
        return rc;
    }
    sslc_ready = 1;

    /* libcurl uses libctru sockets and mbedTLS on the 3DS. */
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        sslcExit();
        sslc_ready = 0;
        socExit();
        free(soc_buffer);
        soc_buffer = NULL;
        return -1;
    }

    network_ready = 1;
    return 0;
}

static void cleanup_network(void)
{
    if (network_ready) {
        curl_global_cleanup();
        network_ready = 0;
    }

    if (sslc_ready) {
        sslcExit();
        sslc_ready = 0;
    }

    if (soc_buffer) {
        socExit();
        free(soc_buffer);
        soc_buffer = NULL;
    }
}

static Result api_request(
    HTTPC_RequestMethod method,
    const char *path,
    const char *body,
    const char *auth_token,
    const char *captcha_token,
    char *response,
    size_t response_size,
    u32 *http_status,
    char *detail,
    size_t detail_size)
{
    CURL *easy = NULL;
    CURLcode cc;
    struct curl_slist *headers = NULL;
    char url[512];
    static char auth_header[TOKEN_MAX + 16];
    static char captcha_header[4200];
    ResponseBuffer output;
    char curl_error[CURL_ERROR_SIZE];
    long status = 0;
    long os_error = 0;
    long verify_result = 0;
    Result result = -1;

    if (detail && detail_size)
        detail[0] = '\0';

    if (!network_ready || !path || !response ||
        response_size < 2) {
        set_error(detail, detail_size, "Invalid request", -1);
        return -1;
    }

    snprintf(url, sizeof(url), "%s%s", API_BASE, path);
    response[0] = '\0';

    if (http_status)
        *http_status = 0;

    output.data = response;
    output.capacity = response_size;
    output.used = 0;
    curl_error[0] = '\0';

    easy = curl_easy_init();
    if (!easy) {
        if (detail && detail_size)
            snprintf(detail, detail_size, "Could not create HTTPS client");
        goto done;
    }

    if (!append_curl_header(&headers, "Accept: application/json")) {
        if (detail && detail_size)
            snprintf(detail, detail_size, "Could not create HTTP headers");
        goto done;
    }

    if (auth_token && auth_token[0]) {
        snprintf(auth_header, sizeof(auth_header),
                 "Authorization: Bearer %s", auth_token);
        if (!append_curl_header(&headers, auth_header)) {
            if (detail && detail_size)
                snprintf(detail, detail_size, "Could not create authorization header");
            goto done;
        }
    }

    if (captcha_token && captcha_token[0]) {
        snprintf(captcha_header, sizeof(captcha_header),
                 "X-Captcha-Token: %s", captcha_token);
        if (!append_curl_header(&headers, captcha_header)) {
            if (detail && detail_size)
                snprintf(detail, detail_size, "Could not create CAPTCHA header");
            goto done;
        }
    }

    if (body) {
        if (!append_curl_header(&headers,
                                "Content-Type: application/json")) {
            if (detail && detail_size)
                snprintf(detail, detail_size, "Could not create content-type header");
            goto done;
        }
    }

#define CURL_SETOPT(option, value) do { \
        cc = curl_easy_setopt(easy, option, value); \
        if (cc != CURLE_OK) { \
            if (detail && detail_size) \
                snprintf(detail, detail_size, "%s: %s", \
                         #option, curl_easy_strerror(cc)); \
            goto done; \
        } \
    } while (0)

    CURL_SETOPT(CURLOPT_URL, url);
    CURL_SETOPT(CURLOPT_HTTPHEADER, headers);
    CURL_SETOPT(CURLOPT_WRITEFUNCTION, fluxer_curl_write_callback);
    CURL_SETOPT(CURLOPT_WRITEDATA, &output);
    CURL_SETOPT(CURLOPT_USERAGENT, "Fluxer3DS/0.4");
    CURL_SETOPT(CURLOPT_ERRORBUFFER, curl_error);
    CURL_SETOPT(CURLOPT_CAINFO, "sdmc:/config/ssl/cacert.pem");
    CURL_SETOPT(CURLOPT_NOSIGNAL, 1L);
    CURL_SETOPT(CURLOPT_CONNECTTIMEOUT, 25L);
    CURL_SETOPT(CURLOPT_TIMEOUT, 75L);

    /* Require modern TLS and keep certificate/hostname checks enabled. */
    CURL_SETOPT(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    CURL_SETOPT(CURLOPT_SSL_VERIFYPEER, 1L);
    CURL_SETOPT(CURLOPT_SSL_VERIFYHOST, 2L);

    if (method == HTTPC_METHOD_POST) {
        CURL_SETOPT(CURLOPT_POST, 1L);
        if (body) {
            CURL_SETOPT(CURLOPT_POSTFIELDS, body);
            CURL_SETOPT(CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        }
    } else {
        CURL_SETOPT(CURLOPT_HTTPGET, 1L);
    }

    cc = curl_easy_perform(easy);
    if (cc != CURLE_OK) {
        curl_easy_getinfo(easy, CURLINFO_OS_ERRNO, &os_error);
        curl_easy_getinfo(easy, CURLINFO_SSL_VERIFYRESULT, &verify_result);
        if (detail && detail_size) {
            if (curl_error[0])
                snprintf(detail, detail_size,
                         "HTTPS: %.72s (curl %d, OS %ld, cert %ld)",
                         curl_error, (int)cc, os_error, verify_result);
            else
                snprintf(detail, detail_size,
                         "HTTPS: %s (curl %d, OS %ld, cert %ld)",
                         curl_easy_strerror(cc), (int)cc,
                         os_error, verify_result);
        }
        goto done;
    }

    cc = curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    if (cc != CURLE_OK) {
        if (detail && detail_size)
            snprintf(detail, detail_size, "Reading HTTP status: %s",
                     curl_easy_strerror(cc));
        goto done;
    }

    if (http_status)
        *http_status = (u32)status;

    result = 0;

#undef CURL_SETOPT

done:
    if (headers)
        curl_slist_free_all(headers);
    if (easy)
        curl_easy_cleanup(easy);

    return result;
}

/* ---------------------------------------------------------
   NINTENDO 3DS KEYBOARD
   --------------------------------------------------------- */

static int keyboard_input(
    const char *hint,
    char *output,
    size_t output_size,
    int password_mode)
{
    SwkbdState keyboard;

    memset(output, 0, output_size);

    swkbdInit(
        &keyboard,
        SWKBD_TYPE_NORMAL,
        2,
        (int)output_size - 1
    );

    swkbdSetHintText(&keyboard, hint);

    if (password_mode) {
        swkbdSetPasswordMode(
            &keyboard, SWKBD_PASSWORD_HIDE);
    }

    SwkbdButton result = swkbdInputText(
        &keyboard, output, output_size);

    if (result != SWKBD_BUTTON_CONFIRM) {
        memset(output, 0, output_size);
        return 0;
    }

    return output[0] != '\0';
}

/* ---------------------------------------------------------
   ACCOUNT LOGIN
   --------------------------------------------------------- */

static void show_api_error(
    u32 http_status,
    const char *response)
{
    char error_code[96];

    error_code[0] = '\0';

    json_string(
        response, "code",
        error_code, sizeof(error_code));

    if (strcmp(error_code, "INVALID_EMAIL_OR_PASSWORD") == 0 ||
        strcmp(error_code, "INVALID_LOGIN_OR_PASSWORD") == 0) {
        snprintf(status_text, sizeof(status_text),
                 "Incorrect email or password.");
    }
    else if (strcmp(error_code, "CAPTCHA_REQUIRED") == 0 ||
             strcmp(error_code, "INVALID_CAPTCHA") == 0) {
        snprintf(status_text, sizeof(status_text),
                 "Fluxer requires CAPTCHA verification. "
                 "This flow is not implemented yet.");
    }
    else if (strcmp(error_code, "IP_AUTHORIZATION_REQUIRED") == 0) {
        snprintf(status_text, sizeof(status_text),
                 "Check your email to authorize this new IP. "
                 "Authorization polling is not implemented yet.");
    }
    else if (strcmp(error_code, "SSO_REQUIRED") == 0) {
        snprintf(status_text, sizeof(status_text),
                 "This account requires organization sign-in.");
    }
    else {
        snprintf(status_text, sizeof(status_text),
                 "Fluxer rejected login. HTTP %lu. %.65s",
                 (unsigned long)http_status,
                 error_code[0] ? error_code : "Unknown error");
    }

    app_state = 3;
}

static int accept_auth_response(
    const char *response)
{
    char token[TOKEN_MAX];
    char ticket[256];

    token[0] = '\0';
    ticket[0] = '\0';

    /*
       Normal successful login returns a token.
    */
    if (json_string(
            response, "token",
            token, sizeof(token))) {

        snprintf(session_token,
                 sizeof(session_token), "%s", token);

        memset(token, 0, sizeof(token));

        return 1;
    }

    /*
       MFA response returns mfa=true and a ticket.
    */
    if (strstr(response, "\"mfa\":true") ||
        strstr(response, "\"mfa\": true")) {

        if (json_string(
                response, "ticket",
                ticket, sizeof(ticket))) {

            snprintf(mfa_ticket,
                     sizeof(mfa_ticket), "%s", ticket);

            memset(ticket, 0, sizeof(ticket));

            app_state = 1;

            snprintf(status_text, sizeof(status_text),
                     "Enter your authenticator code.");

            return 2;
        }
    }

    return 0;
}

#define SESSION_DIR "sdmc:/3ds/Fluxer3DS"
#define SESSION_FILE SESSION_DIR "/session.dat"

static void delete_saved_session(void)
{
    remove(SESSION_FILE);
}

static int save_session_token(void)
{
    FILE *file;
    size_t len;
    if (!session_token[0]) return 0;
    len = strlen(session_token);
    if (len == 0 || len >= TOKEN_MAX || strchr(session_token, '\n') || strchr(session_token, '\r')) return 0;
    mkdir("sdmc:/3ds", 0777);
    mkdir(SESSION_DIR, 0777);
    file = fopen(SESSION_FILE, "wb");
    if (!file) return 0;
    if (fwrite(session_token, 1, len, file) != len) {
        fclose(file);
        delete_saved_session();
        return 0;
    }
    if (fclose(file) != 0) {
        delete_saved_session();
        return 0;
    }
    return 1;
}

static int load_session_token(void)
{
    FILE *file = fopen(SESSION_FILE, "rb");
    size_t len;
    int extra;
    if (!file) return 0;
    len = fread(session_token, 1, sizeof(session_token) - 1, file);
    extra = fgetc(file);
    fclose(file);
    session_token[len] = '\0';
    if (len == 0 || extra != EOF || strchr(session_token, '\n') || strchr(session_token, '\r')) {
        memset(session_token, 0, sizeof(session_token));
        delete_saved_session();
        return 0;
    }
    session_loaded_from_disk = 1;
    return 1;
}

static void verify_session(void)
{
    static char response[HTTP_BODY_MAX];
    char name[96];
    char detail[128];

    u32 http_status = 0;

    Result rc = api_request(
        HTTPC_METHOD_GET,
        "/users/@me",
        NULL,
        session_token,
        NULL,
        response,
        sizeof(response),
        &http_status,
        detail,
        sizeof(detail)
    );

    if (R_FAILED(rc)) {
        memset(session_token, 0, sizeof(session_token));

        snprintf(status_text, sizeof(status_text),
                 "Account check failed: %.100s", detail);

        app_state = 3;
        return;
    }

    if (http_status != 200) {
        if (http_status == 401 || http_status == 403) delete_saved_session();
        memset(session_token, 0, sizeof(session_token));

        snprintf(status_text, sizeof(status_text),
                 "Account check HTTP error: %lu",
                 (unsigned long)http_status);

        app_state = 3;
        return;
    }

    name[0] = '\0';

    if (!json_string(
            response, "global_name",
            name, sizeof(name)) &&
        !json_string(
            response, "username",
            name, sizeof(name))) {

        snprintf(name, sizeof(name),
                 "Fluxer account");
    }

    snprintf(account_name, sizeof(account_name),
             "%s", name);

    if (save_session_token())
        snprintf(status_text, sizeof(status_text), "Signed in. Login saved on SD card.");
    else
        snprintf(status_text, sizeof(status_text), "Signed in, but could not save login to SD card.");

    app_state = 2;
    load_home_data();
    /* Reveal the home screen after the account has been verified. */
    login_unzip_active = 1;
    login_unzip_started = osGetTime();
}

static void submit_password_login(void)
{
    char login[INPUT_MAX];
    char password[INPUT_MAX];

    char escaped_login[INPUT_MAX * 2];
    char escaped_password[INPUT_MAX * 2];

    static char response[HTTP_BODY_MAX];
    char detail[128];

    u32 http_status = 0;

    /*
       The JSON body is aligned for libctru's
       raw POST data function.
    */
    char body[INPUT_MAX * 4 + 128]
        __attribute__((aligned(4)));

    memset(login, 0, sizeof(login));
    memset(password, 0, sizeof(password));
    memset(body, 0, sizeof(body));

    snprintf(status_text, sizeof(status_text),
             "Enter your Fluxer email.");

    if (!keyboard_input(
            "Email address or username",
            login, sizeof(login), 0)) {
        snprintf(status_text, sizeof(status_text),
                 "Sign-in cancelled.");
        return;
    }

    snprintf(status_text, sizeof(status_text),
             "Enter your Fluxer password.");

    if (!keyboard_input(
            "Password",
            password, sizeof(password), 1)) {
        memset(login, 0, sizeof(login));

        snprintf(status_text, sizeof(status_text),
                 "Sign-in cancelled.");
        return;
    }

    if (!json_escape(
            login, escaped_login,
            sizeof(escaped_login)) ||
        !json_escape(
            password, escaped_password,
            sizeof(escaped_password))) {

        snprintf(status_text, sizeof(status_text),
                 "Could not prepare login data.");
        app_state = 3;
        goto cleanup;
    }

    snprintf(body, sizeof(body),
             "{\"login\":\"%s\",\"password\":\"%s\"}",
             escaped_login, escaped_password);

    snprintf(status_text, sizeof(status_text),
             "Contacting Fluxer securely...");

    Result rc = api_request(
        HTTPC_METHOD_POST,
        "/auth/login",
        body,
        NULL,
        NULL,
        response,
        sizeof(response),
        &http_status,
        detail,
        sizeof(detail)
    );

    if (R_FAILED(rc)) {
        snprintf(status_text, sizeof(status_text),
                 "Login request failed: %.110s", detail);
        app_state = 3;
        goto cleanup;
    }

    if (http_status == 400) {
        char error_code[96];
        error_code[0] = '\0';
        json_string(response, "code", error_code, sizeof(error_code));
        if (strcmp(error_code, "CAPTCHA_REQUIRED") == 0 ||
            strcmp(error_code, "INVALID_CAPTCHA") == 0) {
            if (!altcha_begin(response)) {
                snprintf(status_text, sizeof(status_text),
                         "Could not read Fluxer's CAPTCHA challenge.");
                app_state = 3;
                goto cleanup;
            }
            snprintf(pending_login_body, sizeof(pending_login_body), "%s", body);
            memset(pending_captcha_token, 0, sizeof(pending_captcha_token));
            snprintf(status_text, sizeof(status_text),
                     "Security check started. Please wait...");
            app_state = 4;
            goto cleanup;
        }
    }

    if (http_status != 200) {
        show_api_error(http_status, response);
        goto cleanup;
    }

    int result = accept_auth_response(response);

    if (result == 1) {
        verify_session();
    }
    else if (result == 2) {
        /* MFA screen will ask for the code. */
    }
    else {
        snprintf(status_text, sizeof(status_text),
                 "Unexpected response from Fluxer.");
        app_state = 3;
    }

cleanup:
    memset(login, 0, sizeof(login));
    memset(password, 0, sizeof(password));
    memset(escaped_login, 0, sizeof(escaped_login));
    memset(escaped_password, 0, sizeof(escaped_password));
    memset(body, 0, sizeof(body));
}

static void submit_mfa_code(void)
{
    char code[128];
    static char response[HTTP_BODY_MAX];
    char detail[128];

    char body[512]
        __attribute__((aligned(4)));

    u32 http_status = 0;

    memset(code, 0, sizeof(code));
    memset(body, 0, sizeof(body));

    if (!keyboard_input(
            "Authenticator code or backup code",
            code, sizeof(code), 0)) {
        snprintf(status_text, sizeof(status_text),
                 "MFA entry cancelled.");
        return;
    }

    char escaped_code[256];

    if (!json_escape(
            code, escaped_code,
            sizeof(escaped_code))) {
        snprintf(status_text, sizeof(status_text),
                 "Could not prepare MFA request.");
        goto cleanup;
    }

    snprintf(body, sizeof(body),
             "{\"ticket\":\"%s\",\"code\":\"%s\"}",
             mfa_ticket, escaped_code);

    snprintf(status_text, sizeof(status_text),
             "Checking authenticator code...");

    Result rc = api_request(
        HTTPC_METHOD_POST,
        "/auth/login/mfa/totp",
        body,
        NULL,
        NULL,
        response,
        sizeof(response),
        &http_status,
        detail,
        sizeof(detail)
    );

    if (R_FAILED(rc)) {
        snprintf(status_text, sizeof(status_text),
                 "MFA request failed: %.100s", detail);
        app_state = 3;
        goto cleanup;
    }

    if (http_status != 200) {
        show_api_error(http_status, response);
        goto cleanup;
    }

    int result = accept_auth_response(response);

    if (result == 1) {
        memset(mfa_ticket, 0, sizeof(mfa_ticket));
        verify_session();
    }
    else {
        snprintf(status_text, sizeof(status_text),
                 "MFA response was not recognized.");
        app_state = 3;
    }

cleanup:
    memset(code, 0, sizeof(code));
    memset(escaped_code, 0, sizeof(escaped_code));
    memset(body, 0, sizeof(body));
}

/* ---------------------------------------------------------
   GRAPHICS
   --------------------------------------------------------- */

static void draw_text(
    const char *value,
    float x, float y,
    float scale, u32 color)
{
    C2D_Text text;
    char safe_text[768];
    size_t in = 0, out = 0;

    /*
       The 3DS system font does not contain every Unicode glyph.
       Keep ASCII, transliterate common accented Latin letters, and
       replace each other valid UTF-8 character with '?' so a fancy
       server name can never break text parsing or tile navigation.
    */
    if (!value) value = "";
    while (value[in] && out + 2 < sizeof(safe_text)) {
        unsigned char c = (unsigned char)value[in];
        unsigned int cp;
        size_t bytes;
        if (c < 0x80) {
            safe_text[out++] = (char)c;
            in++;
            continue;
        }
        if ((c & 0xE0) == 0xC0 && value[in+1] &&
            (((unsigned char)value[in+1] & 0xC0) == 0x80)) {
            cp = ((c & 0x1F) << 6) | ((unsigned char)value[in+1] & 0x3F);
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0 && value[in+1] && value[in+2] &&
                   (((unsigned char)value[in+1] & 0xC0) == 0x80) &&
                   (((unsigned char)value[in+2] & 0xC0) == 0x80)) {
            cp = ((c & 0x0F) << 12) |
                 (((unsigned char)value[in+1] & 0x3F) << 6) |
                 ((unsigned char)value[in+2] & 0x3F);
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0 && value[in+1] && value[in+2] && value[in+3] &&
                   (((unsigned char)value[in+1] & 0xC0) == 0x80) &&
                   (((unsigned char)value[in+2] & 0xC0) == 0x80) &&
                   (((unsigned char)value[in+3] & 0xC0) == 0x80)) {
            cp = ((c & 0x07) << 18) |
                 (((unsigned char)value[in+1] & 0x3F) << 12) |
                 (((unsigned char)value[in+2] & 0x3F) << 6) |
                 ((unsigned char)value[in+3] & 0x3F);
            bytes = 4;
        } else {
            /* Skip malformed UTF-8 bytes instead of displaying replacement marks. */
            in++;
            continue;
        }
        in += bytes;
        switch (cp) {
            case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3:
            case 0x00C4: case 0x00C5: case 0x0100: case 0x0102:
            case 0x0104: safe_text[out++] = 'A'; break;
            case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3:
            case 0x00E4: case 0x00E5: case 0x0101: case 0x0103:
            case 0x0105: safe_text[out++] = 'a'; break;
            case 0x00C7: case 0x0106: case 0x010C: safe_text[out++] = 'C'; break;
            case 0x00E7: case 0x0107: case 0x010D: safe_text[out++] = 'c'; break;
            case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
            case 0x0112: case 0x0116: case 0x0118: safe_text[out++] = 'E'; break;
            case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
            case 0x0113: case 0x0117: case 0x0119: safe_text[out++] = 'e'; break;
            case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
            case 0x012A: case 0x012E: safe_text[out++] = 'I'; break;
            case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
            case 0x012B: case 0x012F: safe_text[out++] = 'i'; break;
            case 0x00D1: case 0x0143: case 0x0147: safe_text[out++] = 'N'; break;
            case 0x00F1: case 0x0144: case 0x0148: safe_text[out++] = 'n'; break;
            case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5:
            case 0x00D6: case 0x00D8: case 0x014C: safe_text[out++] = 'O'; break;
            case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5:
            case 0x00F6: case 0x00F8: case 0x014D: safe_text[out++] = 'o'; break;
            case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
            case 0x016A: case 0x0172: safe_text[out++] = 'U'; break;
            case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
            case 0x016B: case 0x0173: safe_text[out++] = 'u'; break;
            case 0x00DD: case 0x0178: safe_text[out++] = 'Y'; break;
            case 0x00FD: case 0x00FF: safe_text[out++] = 'y'; break;
            case 0x00DF: safe_text[out++] = 's'; safe_text[out++] = 's'; break;
            case 0x00C6: safe_text[out++] = 'A'; safe_text[out++] = 'E'; break;
            case 0x00E6: safe_text[out++] = 'a'; safe_text[out++] = 'e'; break;
            case 0x0152: safe_text[out++] = 'O'; safe_text[out++] = 'E'; break;
            case 0x0153: safe_text[out++] = 'o'; safe_text[out++] = 'e'; break;
            /* Unsupported glyphs (emoji, CJK, symbols) are omitted cleanly. */
            default: break;
        }
    }
    safe_text[out] = '\0';

    C2D_TextFontParse(&text, ui_font, text_buffer, safe_text);
    /* Keep each parsed label in the shared frame buffer until frame end. */
    C2D_DrawText(&text, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

static void draw_aero_background(int width, int height) {
    int y;
    /* A small, cheap blue-to-aqua gradient: no textures or large assets. */
    for (y=0; y<height; y+=12) {
        int t=(y*100)/height;
        int r= (u8)(116 + (t*48)/100);
        int g= (u8)(198 + (t*43)/100);
        int b= (u8)(226 + (t*25)/100);
        C2D_DrawRectSolid(0,(float)y,0.5f,(float)width,12.5f,theme_color(r,g,b,255));
    }
    /* Subtle sky-glow bands. */
    C2D_DrawRectSolid(0,0,0.4f,(float)width,3,theme_color(255,255,255,100));
    C2D_DrawRectSolid(0,(float)(height-5),0.4f,(float)width,5,theme_color(25,111,151,35));
}
static void draw_aero_panel(float x,float y,float w,float h) {
    /* Layered shadow, pale bevel and glass highlight give panels a little lift. */
    C2D_DrawRectSolid(x+3,y+5,0.55f,w,h,theme_color(16,73,105,72));
    C2D_DrawRectSolid(x+1,y+2,0.48f,w,h,theme_color(103,174,194,105));
    C2D_DrawRectSolid(x,y,0.40f,w,h,theme_color(235,250,253,245));
    C2D_DrawRectSolid(x+2,y+2,0.30f,w-4,h*0.38f,theme_color(255,255,255,125));
    C2D_DrawRectSolid(x+2,y+2,0.25f,w-4,1,theme_color(255,255,255,220));
    C2D_DrawRectSolid(x+2,y+2,0.25f,1,h-4,theme_color(255,255,255,175));
    C2D_DrawRectSolid(x+2,y+h-3,0.30f,w-4,1,theme_color(105,169,190,100));
}
static void draw_aero_button(float x,float y,float w,float h,const char *label,u32 base) {
    /* A visible lower extrusion plus a bright top bevel makes it feel raised. */
    C2D_DrawRectSolid(x+3,y+5,0.55f,w,h,theme_color(12,69,101,95));
    C2D_DrawRectSolid(x+1,y+2,0.48f,w,h,theme_color(79,151,177,145));
    C2D_DrawRectSolid(x,y,0.40f,w,h,base);
    C2D_DrawRectSolid(x+2,y+2,0.30f,w-4,h*0.43f,theme_color(255,255,255,105));
    C2D_DrawRectSolid(x+2,y+2,0.25f,w-4,1,theme_color(255,255,255,220));
    C2D_DrawRectSolid(x+2,y+2,0.25f,1,h-4,theme_color(255,255,255,150));
    C2D_DrawRectSolid(x+2,y+h-3,0.30f,w-4,1,theme_color(16,83,116,105));
    draw_text(label,x+12,y+h*0.30f,0.40f,theme_color(255,255,255,255));
}
static void draw_zip_cloth(float x,float y,float w,float h) {
    int i;
    C2D_DrawRectSolid(x+3,y+4,0.5f,w,h,theme_color(17,76,100,75));
    C2D_DrawRectSolid(x,y,0.4f,w,h,theme_color(54,137,157,255));
    for(i=0;i<10;i++) C2D_DrawRectSolid(x+5,y+8+i*11,0.3f,w-10,1,theme_color(130,196,205,55));
    C2D_DrawRectSolid(x+5,y+5,0.2f,w-10,2,theme_color(191,231,231,255));
    C2D_DrawRectSolid(x+5,y+h-7,0.2f,w-10,2,theme_color(25,91,111,255));
    C2D_DrawRectSolid(x+6,y+6,0.2f,2,h-12,theme_color(190,230,230,255));
    C2D_DrawRectSolid(x+w-8,y+6,0.2f,2,h-12,theme_color(25,91,111,255));
    C2D_DrawRectSolid(x+12,y+h*0.70f,0.2f,w-24,7,theme_color(28,88,107,255));
    for(i=0;i<(int)(w-32);i+=8) {
        C2D_DrawRectSolid(x+16+i,y+h*0.70f,0.1f,4,3,theme_color(222,240,235,255));
        C2D_DrawRectSolid(x+20+i,y+h*0.70f+4,0.1f,4,3,theme_color(116,169,178,255));
    }
    C2D_DrawRectSolid(x+w*0.48f,y+h*0.70f-5,0.0f,18,17,theme_color(218,231,226,255));
    C2D_DrawRectSolid(x+w*0.48f+4,y+h*0.70f-2,0.0f,10,11,theme_color(104,140,146,255));
    C2D_DrawRectSolid(x+w*0.48f+8,y+h*0.70f+10,0.0f,3,10,theme_color(218,231,226,255));
}
static void draw_unzip_overlay(void) {
    float t=(float)(osGetTime()-login_unzip_started)/850.0f;
    float shift;
    int i;
    if(t>=1.0f) { login_unzip_active=0; return; }
    if(t<0.0f)t=0.0f;
    shift=t*176.0f;
    C2D_DrawRectSolid(-shift,0,0.05f,160,240,theme_color(54,137,157,255));
    C2D_DrawRectSolid(160+shift,0,0.05f,160,240,theme_color(54,137,157,255));
    for(i=0;i<18;i++) {
        float yy=(float)(i*14);
        C2D_DrawRectSolid(-shift+5,yy,0.0f,150,1,theme_color(160,211,216,90));
        C2D_DrawRectSolid(165+shift,yy,0.0f,150,1,theme_color(160,211,216,90));
    }
    if(t<0.78f) {
        int alpha=(int)(255.0f*(1.0f-t/0.78f));
        C2D_DrawRectSolid(157,0,0.0f,6,240,theme_color(27,81,100,(u8)alpha));
        for(i=4;i<236;i+=9) C2D_DrawRectSolid(157,(float)i,0.0f,6,4,theme_color(229,239,229,(u8)alpha));
        C2D_DrawRectSolid(151,118.0f-t*105.0f,0.0f,18,18,theme_color(224,235,227,(u8)alpha));
    }
}
static void draw_aero_tile(float x,float y,float w,float h,const char *symbol,const char *name,int selected) {
    u32 face=selected?theme_color(58,169,205,255):theme_color(225,247,251,245);
    u32 ink=selected?C2D_Color32(255,255,255,255):COL_TEXT;
    /* Simple glossy tile; avoids the heavier discarded bevel pass. */
    C2D_DrawRectSolid(x+2,y+3,0.5f,w,h,theme_color(22,91,119,60));
    C2D_DrawRectSolid(x,y,0.4f,w,h,face);
    C2D_DrawRectSolid(x+2,y+2,0.3f,w-4,h*0.40f,selected?theme_color(255,255,255,75):theme_color(255,255,255,125));
    C2D_DrawRectSolid(x+7,y+7,0.2f,34,h-14,selected?theme_color(24,126,162,255):theme_color(151,216,231,255));
    draw_text(symbol,x+14,y+15,0.43f,C2D_Color32(255,255,255,255));
    draw_text(name,x+48,y+14,0.43f,ink);
}
static void render_bottom(void) {
    int i;
    draw_aero_background(320,240);
    draw_aero_panel(0,0,320,34);
    draw_text("fluxer",12,5,0.68f,COL_TEXT);
    draw_text("COMMUNITY CLIENT",155,9,0.34f,COL_TEXT);
    if(app_state==2)
        draw_aero_button(230,37,82,23,screen_state==4?"BACK":"SETTINGS",theme_color(104,112,184,255));
    if(app_state!=2) {
        if(app_state==0||app_state==3||app_state==4) {
            draw_zip_cloth(10,43,300,130);
            draw_text("Welcome to Fluxer",22,51,0.60f,theme_color(247,255,255,255));
            draw_text("Sign in to your real account.",22,82,0.42f,theme_color(220,244,246,255));
            draw_text(status_text,22,113,0.27f,app_state==3?theme_color(255,211,211,255):theme_color(231,250,250,255));
            draw_aero_button(10,185,300,45,app_state==4?"Verifying security check...":"Sign in with email and password",app_state==4?theme_color(92,165,184,255):COL_ACCENT);
        } else if(app_state==1) {
            draw_zip_cloth(10,43,300,130);
            draw_text("Two-factor authentication",22,51,0.50f,theme_color(247,255,255,255));
            draw_text("Use your authenticator app",22,82,0.41f,theme_color(220,244,246,255));
            draw_text(status_text,22,120,0.28f,theme_color(224,255,239,255));
            draw_aero_button(10,185,300,45,"Enter verification code",COL_ACCENT);
        }
        return;
    }
    if(screen_state==0||screen_state==1||screen_state==3) {
        int total=screen_state==0?guild_count+1:(screen_state==1?channel_count:friend_count);
        draw_text(screen_state==0?"YOUR COMMUNITIES":(screen_state==1?"COMMUNITY CHANNELS":"FRIENDS"),12,38,0.39f,theme_color(255,255,255,255));
        for(i=0;i<6&&list_offset+i<total;i++) {
            int idx=list_offset+i,col=i%2,row=i/2; float x=8+col*154,y=61+row*55; char label[72],symbol[8];
            if(screen_state==0) {
                if(idx==0) { snprintf(label,sizeof(label),"Friends"); snprintf(symbol,sizeof(symbol),"F"); }
                else { snprintf(label,sizeof(label),"%s",guild_items[idx-1].name); snprintf(symbol,sizeof(symbol),"C"); }
            } else if(screen_state==1) {
                /* Channel rows use the familiar channel-type glyphs, not text labels. */
                snprintf(label,sizeof(label),"%s",channel_items[idx].name);
                switch(channel_items[idx].type) {
                    case 0: snprintf(symbol,sizeof(symbol),"#"); break;
                    case 2: snprintf(symbol,sizeof(symbol),"VC"); break;
                    case 4: snprintf(symbol,sizeof(symbol),"DIR"); break;
                    case 5: snprintf(symbol,sizeof(symbol),"ANN"); break;
                    case 15: snprintf(symbol,sizeof(symbol),"FOR"); break;
                    case 16: snprintf(symbol,sizeof(symbol),"MED"); break;
                    default: snprintf(symbol,sizeof(symbol),"CH"); break;
                }
            } else { snprintf(label,sizeof(label),"%s",friend_items[idx].name); snprintf(symbol,sizeof(symbol),"@"); }
            draw_aero_tile(x,y,146,48,symbol,label,idx==selected_index);
        }
        draw_text("D-pad: move   A: open   B: back",8,224,0.24f,theme_color(255,255,255,255));
        draw_aero_button(264,216,22,20,"^",COL_ACCENT);
        draw_aero_button(289,216,22,20,"v",COL_ACCENT);
    } else if(screen_state==4) {
        draw_text("THEME SETTINGS",14,43,0.56f,COL_TEXT);
        draw_text("PRIMARY THEME COLOR",14,70,0.32f,COL_MUTED);
        draw_aero_button(14,82,132,25,"PRIMARY",theme_edit_target==0?COL_GREEN:COL_ACCENT);
        C2D_DrawRectSolid(158,82,0.1f,38,25,C2D_Color32(theme_primary[0],theme_primary[1],theme_primary[2],255));
        draw_text("SECONDARY THEME COLOR",14,115,0.32f,COL_MUTED);
        draw_aero_button(14,127,132,25,"SECONDARY",theme_edit_target==1?COL_GREEN:COL_ACCENT);
        C2D_DrawRectSolid(158,127,0.1f,38,25,C2D_Color32(theme_secondary[0],theme_secondary[1],theme_secondary[2],255));
        if(theme_wheel_open) {
            draw_theme_color_wheel();
            C2D_DrawRectSolid(205,157,0.1f,70,48,theme_edit_target?C2D_Color32(theme_secondary[0],theme_secondary[1],theme_secondary[2],255):C2D_Color32(theme_primary[0],theme_primary[1],theme_primary[2],255));
            draw_text("COLOR PREVIEW",205,211,0.27f,COL_MUTED);
        } else {
            draw_text("Tap a color square",72,177,0.42f,COL_TEXT);
            draw_text("to open its color wheel",72,198,0.32f,COL_MUTED);
        }
        draw_text("Touch wheel to choose",173,76,0.29f,COL_TEXT);
        draw_text("A: choose   B: back",14,229,0.30f,COL_TEXT);
    } else if(screen_state==2) {
        static const char *faces[]={":)",":(",":D",";)","<3",":P","xD",":O"};
        draw_text("MESSAGE COMPOSER",12,40,0.39f,theme_color(255,255,255,255));
        if(emoji_panel_open) {
            for(i=0;i<8;i++) { int col=i%4,row=i/4; float x=8+col*78,y=66+row*46; draw_aero_button(x,y,72,38,faces[i],COL_ACCENT); }
        } else {
            draw_aero_panel(8,65,304,96);
            draw_text("Draft:",16,72,0.37f,COL_MUTED);
            draw_text(message_draft[0]?message_draft:"Your message will appear here.",16,98,0.40f,message_draft[0]?COL_TEXT:COL_MUTED);
        }
        draw_aero_button(8,174,174,38,"KEYBOARD",theme_color(74,164,194,255));
        draw_aero_button(188,174,40,38,":)",COL_ACCENT);
        draw_aero_button(232,174,80,38,"SEND",COL_GREEN);
        draw_text("B: back     X: emoji panel",10,222,0.26f,theme_color(255,255,255,255));
    }
    if(transition_active) {
        float t=(float)(osGetTime()-transition_started)/320.0f; float size,alpha;
        if(t>1.0f)t=1.0f;
        size=40.0f+280.0f*t; alpha=255.0f*(1.0f-t);
        C2D_DrawRectSolid(0,0,0.1f,320,240,theme_color(12,64,87,(u8)alpha));
        C2D_DrawRectSolid(160-size/2,120-size/2,0.0f,size,size,theme_color(58,169,205,(u8)alpha));
        if(pending_guild_index>=0&&pending_guild_index<guild_count) draw_text(guild_items[pending_guild_index].name,160-size*0.42f,112,0.44f,theme_color(255,255,255,(u8)alpha));
    }
    if(login_unzip_active) draw_unzip_overlay();
}
static void draw_login_spinner(float cx, float cy) {
    static const float dx[8]={0.0f,14.0f,20.0f,14.0f,0.0f,-14.0f,-20.0f,-14.0f};
    static const float dy[8]={-20.0f,-14.0f,0.0f,14.0f,20.0f,14.0f,0.0f,-14.0f};
    unsigned int phase=(unsigned int)(osGetTime()/85ULL)%8U;
    int i;
    for(i=0;i<8;i++) {
        unsigned int age=(unsigned int)((i+8-(int)phase)%8);
        u8 alpha=(u8)(70U+age*25U);
        float radius=(i==(int)phase)?5.0f:3.4f;
        u32 color=theme_color(55,170,207,alpha);
        C2D_DrawCircleSolid(cx+dx[i],cy+dy[i],0.0f,radius,color);
    }
}

static void draw_wrapped_message(const char *value,float x,float y,
                                  float scale,u32 color,int max_lines) {
    const int max_chars=60;
    const char *p=value?value:"";
    int line=0;
    while(*p && line<max_lines) {
        char part[80];
        int n=0,last_space=-1;
        const char *start=p;
        while(*p && *p!='\\n' && n<max_chars) {
            if(*p==' ') last_space=n;
            p++; n++;
        }
        if(*p && *p!='\\n' && n==max_chars && last_space>0) {
            p=start+last_space;
            n=last_space;
        }
        if(n<=0 && *p=='\\n') { p++; line++; continue; }
        if(n>=(int)sizeof(part)) n=(int)sizeof(part)-1;
        memcpy(part,start,(size_t)n);
        part[n]='\\0';
        if(line==max_lines-1 && (p[0] && p[0]!='\\n')) {
            if(n>3) { part[n-3]='.'; part[n-2]='.'; part[n-1]='.'; }
        }
        draw_text(part,x,y+(float)line*9.0f,scale,color);
        line++;
        if(*p==' ') p++;
        else if(*p=='\\n') p++;
    }
}

static void render_top(void) {
    int i;
    draw_aero_background(400,240);
    if(app_state==2&&screen_state==2) {
        draw_aero_panel(8,7,384,36);
        draw_text(selected_channel_name,18,12,0.62f,COL_TEXT);
        draw_text(account_name,282,18,0.34f,COL_GREEN);
        for(i=0;i<3&&i+list_offset<message_count;i++) {
            int idx=message_count-1-(list_offset+i); float y=49+i*56;
            draw_aero_panel(10,y,380,52);
            draw_text(message_items[idx].author,17,y+2,0.39f,COL_ACCENT);
            draw_wrapped_message(message_items[idx].content,17,y+14,0.36f,COL_TEXT,4);
        }
        draw_text(home_status,14,190,0.34f,theme_color(255,255,255,255));
        draw_text("Circle Pad: scroll   B: tiles",14,213,0.34f,theme_color(255,255,255,255));
    } else if(app_state==2) {
        draw_aero_panel(14,12,372,212);
        draw_text("FLUXER",28,25,0.86f,COL_TEXT);
        draw_text("A little place for your communities.",30,67,0.46f,COL_MUTED);
        C2D_DrawRectSolid(30,98,0.5f,340,2,theme_color(117,190,207,255));
        draw_text(account_name,30,116,0.48f,COL_GREEN);
        if(screen_state==0) { draw_text("Choose Friends to open your friend tiles,",30,155,0.42f,COL_TEXT); draw_text("or choose a community to browse its channels.",30,183,0.42f,COL_TEXT); }
        else if(screen_state==1) { draw_text("Community channels",30,155,0.48f,COL_TEXT); draw_text(home_status,30,185,0.38f,COL_MUTED); }
        else if(screen_state==3) { draw_text("Your friends",30,155,0.48f,COL_TEXT); draw_text(home_status,30,185,0.38f,COL_MUTED); }
        else { draw_text("Theme settings",30,155,0.48f,COL_TEXT); draw_text("Choose your primary and secondary colors.",30,185,0.38f,COL_MUTED); }
    } else {
        draw_aero_panel(22,14,356,210);
        draw_text("Fluxer",38,29,0.92f,COL_TEXT);
        if(app_state==4) {
            draw_text("Signing in securely",38,82,0.50f,COL_TEXT);
            draw_text("Completing the CAPTCHA check...",38,111,0.40f,COL_MUTED);
            draw_login_spinner(200.0f,174.0f);
        } else {
            draw_text("Unofficial Fluxer client for 3DS",38,75,0.40f,COL_MUTED);
            C2D_DrawRectSolid(38,112,0.5f,324,2,theme_color(117,190,207,255));
            draw_text("Sign in to your Fluxer account",38,139,0.46f,COL_TEXT);
            draw_text("Secure connection  |  HTTPS",38,172,0.38f,COL_MUTED);
        }
    }
    draw_text("*playing : title.wma",12,229,0.30f,COL_MUTED);
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    gfxInitDefault();

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    /* This is a painter-ordered 2D interface. Citro2D defaults to GEQUAL
       depth testing, so decorative shadow quads at z=0.55 can occlude later
       panels and text at lower/equal depths, leaving dark blocks. Disable
       depth testing so later UI layers render over earlier ones. */
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);

    C3D_RenderTarget *top =
        C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

    C3D_RenderTarget *bottom =
        C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    load_ui_theme();

    ui_font = C2D_FontLoadSystem(CFG_REGION_USA);
    text_buffer = C2D_TextBufNew(16384);

    Result network_rc = init_network();

    if (R_FAILED(network_rc)) {
        snprintf(status_text, sizeof(status_text),
                 "Network init failed: 0x%08lX",
                 (unsigned long)network_rc);

        app_state = 3;
    } else if (load_session_token()) {
        snprintf(status_text, sizeof(status_text), "Restoring saved Fluxer session...");
        verify_session();
        if (app_state != 2 && session_loaded_from_disk && !session_token[0])
            session_loaded_from_disk = 0;
    }

    while (aptMainLoop()) {
        hidScanInput();

        u32 down = hidKeysDown();
        circlePosition circle;
        hidCircleRead(&circle);
        if(app_state==2 && screen_state==2) {
            /* Up on the Circle Pad means older messages (larger offset). */
            if(circle.dy > 60 && circle_scroll_latch != 1) {
                int max_scroll=message_count>5?message_count-5:0;
                if(list_offset<max_scroll) list_offset++;
                circle_scroll_latch=1;
            } else if(circle.dy < -60 && circle_scroll_latch != -1) {
                if(list_offset>0) list_offset--;
                circle_scroll_latch=-1;
            } else if(circle.dy > -20 && circle.dy < 20) {
                circle_scroll_latch=0;
            }
        }

        touchPosition touch;
        int touched=0,tile_touched=0,tile_index=-1;
        int composer_keyboard=0,composer_emoji=0,composer_send=0;
        int settings_touched=0,settings_primary_touch=0,settings_secondary_touch=0;
        int settings_primary_swatch=0,settings_secondary_swatch=0;
        if(down&KEY_TOUCH) {
            hidTouchRead(&touch);
            if(app_state==2 && touch.py>=36 && touch.py<=60 && touch.px>=230 && touch.px<=314)
                settings_touched=1;
            if(app_state==2 && screen_state==4 && touch.py>=82 && touch.py<=107 && touch.px>=14 && touch.px<=196)
                settings_primary_touch=1;
            if(app_state==2 && screen_state==4 && touch.py>=127 && touch.py<=152 && touch.px>=14 && touch.px<=196)
                settings_secondary_touch=1;
            if(app_state==2 && screen_state==4 && theme_wheel_open &&
               touch.py>=108 && touch.py<=239 && touch.px>=30 && touch.px<=162) {
                choose_theme_color(touch.px,touch.py);
            }
            touched=touch.px>=10&&touch.px<=310&&touch.py>=185&&touch.py<=230;
            if(app_state==2&&screen_state!=2&&touch.py>=61&&touch.py<214&&touch.px>=8&&touch.px<=312) {
                int col=touch.px<160?0:1,row=(touch.py-61)/55;
                if(row>=0&&row<3) { tile_touched=1; tile_index=list_offset+row*2+col; }
            }
            if(app_state==2&&screen_state!=2&&touch.py>=216&&touch.py<=239&&touch.px>=264&&touch.px<=312) {
                int total=screen_state==0?guild_count+1:(screen_state==1?channel_count:friend_count);
                if(touch.px<287) { if(list_offset>=2)list_offset-=2; else list_offset=0; }
                else if(list_offset+6<total) list_offset+=2;
                selected_index=list_offset;
            }
            if(app_state==2&&screen_state==2) {
                composer_keyboard=touch.py>=174&&touch.py<=212&&touch.px>=8&&touch.px<184;
                composer_emoji=touch.py>=174&&touch.py<=212&&touch.px>=188&&touch.px<228;
                composer_send=touch.py>=174&&touch.py<=212&&touch.px>=232&&touch.px<=312;
                if(emoji_panel_open&&touch.py>=66&&touch.py<150&&touch.px>=8&&touch.px<=312) {
                    int ei=((touch.py-66)/46)*4+(touch.px-8)/78;
                    static const char *faces[]={":)",":(",":D",";)","<3",":P","xD",":O"};
                    if(ei>=0&&ei<8) append_emoticon(faces[ei]);
                }
            }
        }

        if (down & KEY_START)
            break;

        if (app_state == 4) {
            int solved = altcha_step(2, pending_captcha_token, sizeof(pending_captcha_token));
            if (solved == 0) {
                if ((altcha_progress() & 31UL) == 0)
                    snprintf(status_text, sizeof(status_text), "Verifying CAPTCHA... %lu attempts", altcha_progress());
            } else if (solved < 0) {
                snprintf(status_text, sizeof(status_text), "CAPTCHA could not be solved. Please try again.");
                app_state = 3;
                memset(pending_login_body, 0, sizeof(pending_login_body));
            } else {
                u32 captcha_status = 0;
                char captcha_detail[128];
                snprintf(status_text, sizeof(status_text), "Security check complete. Signing in...");
                Result captcha_rc = api_request(HTTPC_METHOD_POST, "/auth/login",
                    pending_login_body, NULL, pending_captcha_token,
                    captcha_retry_response, sizeof(captcha_retry_response),
                    &captcha_status, captcha_detail, sizeof(captcha_detail));
                memset(pending_captcha_token, 0, sizeof(pending_captcha_token));
                memset(pending_login_body, 0, sizeof(pending_login_body));
                if (R_FAILED(captcha_rc)) {
                    snprintf(status_text, sizeof(status_text), "CAPTCHA retry failed: %.90s", captcha_detail);
                    app_state = 3;
                } else if (captcha_status != 200) {
                    show_api_error(captcha_status, captcha_retry_response);
                } else {
                    int auth_result = accept_auth_response(captcha_retry_response);
                    if (auth_result == 1) verify_session();
                    else if (auth_result != 2) {
                        snprintf(status_text, sizeof(status_text), "Unexpected response after CAPTCHA.");
                        app_state = 3;
                    }
                }
            }
        }

        if ((app_state == 0 || app_state == 3) &&
            ((down & KEY_A) || touched)) {

            submit_password_login();
        }
        else if (app_state == 1 &&
                 ((down & KEY_A) || touched)) {

            submit_mfa_code();
        }
        else if(app_state==2) {
            int total=screen_state==0?guild_count+1:(screen_state==1?channel_count:(screen_state==3?friend_count:message_count));
            if(settings_touched) {
                if(screen_state==4) { screen_state=settings_return_screen; theme_wheel_open=0; }
                else { settings_return_screen=screen_state; screen_state=4; selected_index=0; }
            } else if(screen_state==4) {
                if(settings_primary_swatch) {
                    theme_edit_target=0; theme_wheel_open=1; selected_index=0;
                } else if(settings_secondary_swatch) {
                    theme_edit_target=1; theme_wheel_open=1; selected_index=1;
                } else if(settings_primary_touch || ((down&KEY_A)&&selected_index==0)) {
                    theme_edit_target=0; theme_wheel_open=1; selected_index=0;
                } else if(settings_secondary_touch || ((down&KEY_A)&&selected_index==1)) {
                    theme_edit_target=1; theme_wheel_open=1; selected_index=1;
                } else if(down&KEY_B) { if(theme_wheel_open) theme_wheel_open=0; else screen_state=settings_return_screen; }
                else if(down&KEY_UP) { selected_index=(selected_index+1)%2; }
                else if(down&KEY_DOWN) { selected_index=(selected_index+1)%2; }
            } else if(down&KEY_X) {
                if(screen_state==2) emoji_panel_open=!emoji_panel_open;
                else {
                    delete_saved_session(); memset(session_token,0,sizeof(session_token)); memset(account_name,0,sizeof(account_name));
                    snprintf(account_name,sizeof(account_name),"Not signed in");
                    dm_count=guild_count=channel_count=message_count=friend_count=0;
                    screen_state=0; selected_index=0; list_offset=0; app_state=0;
                    snprintf(status_text,sizeof(status_text),"Logged out. Saved session deleted.");
                }
            } else if(screen_state==2&&composer_keyboard) open_message_keyboard();
            else if(screen_state==2&&composer_emoji) emoji_panel_open=!emoji_panel_open;
            else if(screen_state==2&&composer_send) send_chat_message();
            else if(screen_state==2&&(down&KEY_Y)) open_message_keyboard();
            else if(screen_state==2&&(down&KEY_A)) send_chat_message();
            else if(down&KEY_B) {
                if(screen_state==2) { screen_state=chat_return_screen; selected_index=chat_return_selection; list_offset=(selected_index/6)*6; emoji_panel_open=0; }
                else if(screen_state==1||screen_state==3) { screen_state=0; selected_index=0; list_offset=0; }
            } else if(screen_state!=2&&(down&KEY_RIGHT)) { if(total>0&&selected_index<total-1)selected_index++; }
            else if(screen_state!=2&&(down&KEY_LEFT)) { if(selected_index>0)selected_index--; }
            else if(screen_state!=2&&(down&KEY_DOWN)) { if(selected_index+2<total)selected_index+=2; else if(selected_index<total-1)selected_index=total-1; }
            else if(screen_state!=2&&(down&KEY_UP)) { if(selected_index>=2)selected_index-=2; else selected_index=0; }
            if(screen_state!=2) {
                /* Scroll one row at a time and keep the highlighted tile visible. */
                if(selected_index<list_offset) list_offset=(selected_index/2)*2;
                if(selected_index>=list_offset+6) list_offset=((selected_index/2)-2)*2;
                if(list_offset<0) list_offset=0;
                if(tile_touched&&tile_index>=0&&tile_index<total)selected_index=tile_index;
                if((down&KEY_A)||tile_touched) {
                    if(screen_state==0) {
                        if(selected_index==0) { screen_state=3; selected_index=0; list_offset=0; snprintf(home_status,sizeof(home_status),"%d friends",friend_count); }
                        else { pending_guild_index=selected_index-1; transition_active=1; transition_started=osGetTime(); }
                    } else if(screen_state==1&&selected_index<channel_count) {
                        int type=channel_items[selected_index].type;
                        if(type==4)snprintf(home_status,sizeof(home_status),"Category tile: channels are listed in this community.");
                        else if(type==15||type==16)snprintf(home_status,sizeof(home_status),"Forum/media posts are not supported yet.");
                        else if(type==998)snprintf(home_status,sizeof(home_status),"This channel links to an external destination.");
                        else load_messages(channel_items[selected_index].id,channel_items[selected_index].name,0,selected_index);
                    } else if(screen_state==3&&selected_index<friend_count) {
                        int di=friend_items[selected_index].dm_index;
                        if(di>=0) {
                            load_messages(dm_items[di].id,friend_items[selected_index].name,1,di);
                        } else {
                            char create_body[96],create_detail[128];
                            static char create_response[HTTP_BODY_MAX];
                            u32 create_code=0;
                            snprintf(create_body,sizeof(create_body),"{\"recipient_id\":\"%s\"}",friend_items[selected_index].id);
                            Result create_rc=api_request(HTTPC_METHOD_POST,"/users/@me/channels",create_body,session_token,NULL,create_response,sizeof(create_response),&create_code,create_detail,sizeof(create_detail));
                            if(R_FAILED(create_rc)||(create_code!=200&&create_code!=201)) {
                                snprintf(home_status,sizeof(home_status),"Open DM failed: HTTP %lu %.65s",(unsigned long)create_code,create_detail);
                            } else {
                                char new_channel_id[32];
                                if(json_string(create_response,"id",new_channel_id,sizeof(new_channel_id))) {
                                    int new_index;
                                    if(dm_count<MAX_ITEMS) {
                                        new_index=dm_count++;
                                        memset(&dm_items[new_index],0,sizeof(dm_items[new_index]));
                                        snprintf(dm_items[new_index].id,sizeof(dm_items[new_index].id),"%s",new_channel_id);
                                        snprintf(dm_items[new_index].name,sizeof(dm_items[new_index].name),"%s",friend_items[selected_index].name);
                                        snprintf(dm_items[new_index].peer_id,sizeof(dm_items[new_index].peer_id),"%s",friend_items[selected_index].id);
                                    } else new_index=0;
                                    friend_items[selected_index].dm_index=new_index;
                                    load_messages(new_channel_id,friend_items[selected_index].name,1,new_index);
                                } else snprintf(home_status,sizeof(home_status),"DM opened, but channel ID was missing.");
                            }
                        }
                    }
                }
            } else if(message_count>0) {
                int max_scroll=message_count>3?message_count-3:0;
                /* Newest messages sit at the bottom: Up reveals older history,
                   Down returns toward the newest messages. */
                if(down&KEY_UP) { if(list_offset>0)list_offset--; }
                else if(down&KEY_DOWN) { if(list_offset<max_scroll)list_offset++; }
                selected_index=list_offset;
            }
        }
        if(transition_active&&(osGetTime()-transition_started)>=320) {
            int gi=pending_guild_index; transition_active=0; pending_guild_index=-1;
            if(gi>=0&&gi<guild_count)load_guild_channels(gi);
        }
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TextBufClear(text_buffer);

        C2D_TargetClear(top, COL_BG);
        C2D_SceneBegin(top);
        render_top();

        C2D_TargetClear(bottom, COL_BG);
        C2D_SceneBegin(bottom);
        render_bottom();

        C3D_FrameEnd(0);
    }

    memset(session_token, 0, sizeof(session_token));
    memset(mfa_ticket, 0, sizeof(mfa_ticket));

    cleanup_network();

    C2D_TextBufDelete(text_buffer);
    C2D_FontFree(ui_font);

    C2D_Fini();
    C3D_Fini();
    gfxExit();

    return 0;
}
