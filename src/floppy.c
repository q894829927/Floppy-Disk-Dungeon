#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib,"kernel32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"winmm.lib")

#define MW 20
#define MH 15
#define TILE 10
#define SW 320
#define SH 180
#define MAX_EN 36
#define MAX_FX 32
#define MODS 8
#define MOD_BUDGET 160

enum { WALL, FLOOR, EXIT, CHEST, TRAP };
enum { TITLE, PLAY, UPGRADE, MODULES, HELP, DEAD, WON };

typedef struct { short x,y; unsigned char type,hp,maxhp,turn,armed,alive; } Enemy;
typedef struct { short x,y,life; unsigned char color; } Fx;
typedef struct { short x,y,w,h; } Room;
typedef struct { int best, unlocked; } Save;

static HWND win;
static HDC backdc;
static HBITMAP backbmp;
static HFONT fontSmall,fontTiny,fontBig;
static unsigned int *pixels;
static unsigned int rng=0x1440BEEFu;
static unsigned char map[MH][MW],seen[MH][MW];
static Enemy en[MAX_EN];
static Fx fx[MAX_FX];
static Room rooms[9];
static int roomCount,enCount,mode=TITLE,returnMode=PLAY;
static int px,py,hp,maxhp,atk,def,gold,floorNo,kills,turnNo,hasKey;
static int installed,unlocked,storageUsed,shieldReady,bestFloor;
static int choices[3],messageTimer,shake,animTick;
static char message[64];
static unsigned char waveBuf[44+16000];

static const char *modName[MODS]={"PATCH.HP","BLADE.SYS","SHIELD.DRV","LEECH.DLL","DASH.EXE","SCAN.COM","THORNS.SYS","FIREBALL.BIN"};
static const char *modDesc[MODS]={"Max HP +3","Attack +1","Block first hit/floor","Heal 1 after 3 kills","15% extra turn","Reveal entire map","Return 1 damage","Ranged bolt [F]"};
static const unsigned char modSize[MODS]={48,40,24,40,32,48,32,56};

void *__cdecl memset(void *p,int v,size_t n){ unsigned char *q=(unsigned char*)p; while(n--) *q++=(unsigned char)v; return p; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char *a=(unsigned char*)d; const unsigned char *b=(const unsigned char*)s; while(n--) *a++=*b++; return d; }

static unsigned int rnd(void){ rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static int irand(int n){ return n?(int)(rnd()%(unsigned)n):0; }
static int iabs(int n){ return n<0?-n:n; }
static int dist(int x,int y,int a,int b){ return iabs(x-a)+iabs(y-b); }
static int bit(int n){ return 1<<n; }
static int hasMod(int n){ return (installed&bit(n))!=0; }

static void strcopy(char *d,const char *s,int cap){ int i=0; while(s[i]&&i<cap-1){d[i]=s[i];i++;} d[i]=0; }
static char *addInt(char *p,int n){ char t[12]; int i=0; if(n==0){*p++='0';return p;} if(n<0){*p++='-';n=-n;} while(n){t[i++]=(char)('0'+n%10);n/=10;} while(i)*p++=t[--i]; return p; }
static void status(const char *s){ strcopy(message,s,64); messageTimer=90; }

static COLORREF col(int c){
  static const COLORREF pal[]={RGB(7,10,20),RGB(24,34,52),RGB(44,62,80),RGB(92,110,124),RGB(214,223,230),RGB(57,184,255),RGB(255,205,65),RGB(255,76,96),RGB(76,220,128),RGB(181,103,255),RGB(255,135,50),RGB(60,120,190)};
  return pal[c%12];
}
static void rect(HDC d,int x,int y,int w,int h,int c){ RECT r={x,y,x+w,y+h}; HBRUSH b=CreateSolidBrush(col(c)); FillRect(d,&r,b); DeleteObject(b); }
static void frame(HDC d,int x,int y,int w,int h,int c){ HPEN p=CreatePen(PS_SOLID,1,col(c)); HGDIOBJ o=SelectObject(d,p); MoveToEx(d,x,y,0); LineTo(d,x+w-1,y); LineTo(d,x+w-1,y+h-1); LineTo(d,x,y+h-1); LineTo(d,x,y); SelectObject(d,o); DeleteObject(p); }
static void txt(HDC d,int x,int y,int c,const char *s){ SetTextColor(d,col(c)); SetBkMode(d,TRANSPARENT); TextOutA(d,x,y,s,lstrlenA(s)); }
static void txtInt(HDC d,int x,int y,int c,const char *label,int value,const char *suffix){ char b[64],*p=b; while(*label)*p++=*label++; p=addInt(p,value); while(*suffix)*p++=*suffix++; *p=0; txt(d,x,y,c,b); }

static void sound(int freq,int ms,int noise){
  int rate=8000,n=rate*ms/1000,i; unsigned int phase=0,step=(unsigned int)((freq<<16)/rate);
  if(n>16000)n=16000;
  waveBuf[0]='R';waveBuf[1]='I';waveBuf[2]='F';waveBuf[3]='F';
  *(DWORD*)(waveBuf+4)=36+n; waveBuf[8]='W';waveBuf[9]='A';waveBuf[10]='V';waveBuf[11]='E';
  waveBuf[12]='f';waveBuf[13]='m';waveBuf[14]='t';waveBuf[15]=' '; *(DWORD*)(waveBuf+16)=16;
  *(WORD*)(waveBuf+20)=1;*(WORD*)(waveBuf+22)=1;*(DWORD*)(waveBuf+24)=rate;*(DWORD*)(waveBuf+28)=rate;*(WORD*)(waveBuf+32)=1;*(WORD*)(waveBuf+34)=8;
  waveBuf[36]='d';waveBuf[37]='a';waveBuf[38]='t';waveBuf[39]='a';*(DWORD*)(waveBuf+40)=n;
  for(i=0;i<n;i++){ int fade=127-(i*100/n); if(noise)waveBuf[44+i]=(unsigned char)(128+((int)(rnd()&255)-128)*fade/128); else {phase+=step;waveBuf[44+i]=(unsigned char)(128+((phase&0x8000)?fade:-fade));} }
  PlaySoundA((LPCSTR)waveBuf,0,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);
}

static void saveGame(void){
  char path[MAX_PATH]; DWORD n=GetModuleFileNameA(0,path,MAX_PATH),wr; HANDLE f; Save s;
  while(n&&path[n-1]!='\\')n--; path[n]=0; lstrcatA(path,"floppy.sav");
  s.best=bestFloor;s.unlocked=unlocked; f=CreateFileA(path,GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_HIDDEN,0);
  if(f!=INVALID_HANDLE_VALUE){WriteFile(f,&s,sizeof(s),&wr,0);CloseHandle(f);}
}
static void loadGame(void){
  char path[MAX_PATH]; DWORD n=GetModuleFileNameA(0,path,MAX_PATH),rd; HANDLE f; Save s={0,3};
  while(n&&path[n-1]!='\\')n--; path[n]=0; lstrcatA(path,"floppy.sav");
  f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
  if(f!=INVALID_HANDLE_VALUE){ReadFile(f,&s,sizeof(s),&rd,0);CloseHandle(f);}
  bestFloor=s.best;unlocked=s.unlocked; if(unlocked<3)unlocked=3;if(unlocked>MODS)unlocked=MODS;
}

static int walkable(int x,int y){ return x>0&&y>0&&x<MW-1&&y<MH-1&&map[y][x]!=WALL; }
static Enemy *enemyAt(int x,int y){ int i; for(i=0;i<enCount;i++)if(en[i].alive&&en[i].x==x&&en[i].y==y)return &en[i];return 0; }
static int bossAlive(void){ int i;for(i=0;i<enCount;i++)if(en[i].alive&&en[i].type==7)return 1;return 0; }
static void burst(int x,int y,int c){ int i,n=0; for(i=0;i<MAX_FX&&n<7;i++)if(fx[i].life<=0){fx[i].x=(short)(x*TILE+5);fx[i].y=(short)(20+y*TILE+5);fx[i].life=(short)(10+irand(10));fx[i].color=(unsigned char)c;n++;} }
static void reveal(void){ int x,y; for(y=0;y<MH;y++)for(x=0;x<MW;x++)if(hasMod(5)||dist(px,py,x,y)<5)seen[y][x]=1; }

static void damagePlayer(int n,Enemy *src){
  if(hasMod(2)&&shieldReady){shieldReady=0;status("SHIELD.DRV blocked damage");sound(220,70,0);return;}
  n-=def;if(n<1)n=1;hp-=n;shake=5;burst(px,py,7);sound(90,120,1);
  if(src&&hasMod(6)&&src->alive){src->hp--;if(!src->hp){src->alive=0;kills++;}}
  if(hp<=0){hp=0;mode=DEAD;if(floorNo>bestFloor){bestFloor=floorNo;saveGame();}}
}
static void killEnemy(Enemy *e){
  e->alive=0;kills++;gold+=1+irand(4);burst(e->x,e->y,6);sound(520,70,0);
  if(hasMod(3)&&kills%3==0&&hp<maxhp){hp++;status("LEECH.DLL recovered 1 HP");}
}
static void hitEnemy(Enemy *e,int power){
  int dmg=power;if(dmg<1)dmg=1;if(e->hp<=dmg)killEnemy(e);else{e->hp=(unsigned char)(e->hp-dmg);burst(e->x,e->y,10);sound(180,55,0);}
}

static int lineClear(int x,int y,int tx,int ty){
  int dx=(tx>x)-(tx<x),dy=(ty>y)-(ty<y); if(dx&&dy)return 0;x+=dx;y+=dy;
  while(x!=tx||y!=ty){if(map[y][x]==WALL)return 0;x+=dx;y+=dy;}return 1;
}
static void enemyMove(Enemy *e,int dx,int dy,int ghost){
  int nx=e->x+dx,ny=e->y+dy;if(nx==px&&ny==py){damagePlayer(e->type==7?3:1,e);return;}
  if((ghost||walkable(nx,ny))&&!enemyAt(nx,ny)){e->x=(short)nx;e->y=(short)ny;}
}
static void chase(Enemy *e,int away,int ghost){
  int dx=(px>e->x)-(px<e->x),dy=(py>e->y)-(py<e->y),a=irand(2);
  if(away){dx=-dx;dy=-dy;} if(a){if(dy)enemyMove(e,0,dy,ghost);else enemyMove(e,dx,0,ghost);}else{if(dx)enemyMove(e,dx,0,ghost);else enemyMove(e,0,dy,ghost);}
}
static void spawnEnemy(int x,int y,int type){ Enemy *e;if(enCount>=MAX_EN||enemyAt(x,y)||(x==px&&y==py))return;e=&en[enCount++];e->x=(short)x;e->y=(short)y;e->type=(unsigned char)type;e->maxhp=e->hp=(unsigned char)(type==7?16+floorNo*2:2+floorNo/2+(type==4?1:0));e->turn=0;e->armed=0;e->alive=1; }

static void enemyTurn(void){
  int i;turnNo++;
  for(i=0;i<enCount&&hp>0;i++)if(en[i].alive){ Enemy *e=&en[i];int d=dist(e->x,e->y,px,py);e->turn++;
    if(e->type==0){if(!(e->turn&1))chase(e,0,0);}
    else if(e->type==1)chase(e,0,0);
    else if(e->type==2){if((e->x==px||e->y==py)&&d<=6&&lineClear(e->x,e->y,px,py)){damagePlayer(1,e);sound(720,35,0);}else chase(e,d<3,0);}
    else if(e->type==3)chase(e,0,1);
    else if(e->type==4){if(e->armed){if(d<=2)damagePlayer(3,e);burst(e->x,e->y,10);e->alive=0;sound(70,180,1);}else if(d<=2){e->armed=1;status("BOMB armed: move away!");}else chase(e,0,0);}
    else if(e->type==5){if(d<5)chase(e,0,0);else enemyMove(e,irand(3)-1,irand(3)-1,0);}
    else if(e->type==6){if(e->turn%4==0){int sx=e->x+irand(3)-1,sy=e->y+irand(3)-1;if(walkable(sx,sy))spawnEnemy(sx,sy,0);}else if(d<6&&lineClear(e->x,e->y,px,py))damagePlayer(1,e);else chase(e,0,0);}
    else {if(e->hp<e->maxhp/2&&e->turn%3==0){int a;for(a=0;a<4;a++){int sx=e->x+(a==0)-(a==1),sy=e->y+(a==2)-(a==3);if(walkable(sx,sy))spawnEnemy(sx,sy,1);}}else if(d<=1)damagePlayer(3,e);else chase(e,0,0);}
  }
}

static void carve(int x,int y,int w,int h){int a,b;for(b=y;b<y+h;b++)for(a=x;a<x+w;a++)map[b][a]=FLOOR;}
static void corridor(int x,int y,int tx,int ty){ while(x!=tx){map[y][x]=FLOOR;x+=(tx>x)?1:-1;}while(y!=ty){map[y][x]=FLOOR;y+=(ty>y)?1:-1;}map[y][x]=FLOOR; }
static int floorSpot(int *ox,int *oy){int tries=300,x,y;while(tries--){x=1+irand(MW-2);y=1+irand(MH-2);if(map[y][x]==FLOOR&&!enemyAt(x,y)&&dist(px,py,x,y)>4){*ox=x;*oy=y;return 1;}}return 0;}
static void generate(void){
  int x,y,i,j,ok,rx,ry,rw,rh,ex,ey,count;
  memset(map,WALL,sizeof(map));memset(seen,0,sizeof(seen));memset(en,0,sizeof(en));enCount=0;roomCount=0;
  for(i=0;i<160&&roomCount<6+irand(3);i++){rw=3+irand(5);rh=3+irand(4);rx=1+irand(MW-rw-2);ry=1+irand(MH-rh-2);ok=1;
    for(j=0;j<roomCount;j++)if(rx<rooms[j].x+rooms[j].w+1&&rx+rw+1>rooms[j].x&&ry<rooms[j].y+rooms[j].h+1&&ry+rh+1>rooms[j].y)ok=0;
    if(ok){rooms[roomCount].x=(short)rx;rooms[roomCount].y=(short)ry;rooms[roomCount].w=(short)rw;rooms[roomCount].h=(short)rh;carve(rx,ry,rw,rh);
      if(roomCount)corridor(rooms[roomCount-1].x+rooms[roomCount-1].w/2,rooms[roomCount-1].y+rooms[roomCount-1].h/2,rx+rw/2,ry+rh/2);roomCount++;}
  }
  if(roomCount<2){carve(1,1,MW-2,MH-2);rooms[0].x=1;rooms[0].y=1;rooms[0].w=5;rooms[0].h=5;rooms[1].x=MW-6;rooms[1].y=MH-6;rooms[1].w=5;rooms[1].h=5;roomCount=2;}
  px=rooms[0].x+rooms[0].w/2;py=rooms[0].y+rooms[0].h/2;ex=rooms[roomCount-1].x+rooms[roomCount-1].w/2;ey=rooms[roomCount-1].y+rooms[roomCount-1].h/2;map[ey][ex]=EXIT;
  hasKey=0;if(floorSpot(&x,&y))map[y][x]=CHEST;
  count=4+floorNo*2;if(count>24)count=24;
  for(i=0;i<count;i++)if(floorSpot(&x,&y))spawnEnemy(x,y,(floorNo%5==0&&i==0)?7:irand(floorNo<3?3:7));
  for(i=0;i<2+floorNo/3;i++)if(floorSpot(&x,&y))map[y][x]=TRAP;
  shieldReady=1;reveal();status("Find the KEY CHIP, then reach EXIT");
}

static void beginRun(void){
  hp=maxhp=10;atk=2;def=0;gold=0;floorNo=1;kills=0;turnNo=0;installed=0;storageUsed=0;hasKey=0;mode=PLAY;generate();
}
static void recalc(void){storageUsed=0;maxhp=10;atk=2;for(int i=0;i<MODS;i++)if(hasMod(i))storageUsed+=modSize[i];if(hasMod(0))maxhp+=3;if(hasMod(1))atk++;if(hp>maxhp)hp=maxhp;reveal();}
static void makeChoices(void){int i,j;for(i=0;i<3;i++){do{choices[i]=irand(unlocked);}while((i&&choices[i]==choices[0])||(i==2&&choices[i]==choices[1]));for(j=0;j<MODS;j++)if((installed&bit(choices[i]))&&unlocked>3){choices[i]=(choices[i]+1)%unlocked;}}}
static void nextFloor(void){floorNo++;if(floorNo>bestFloor){bestFloor=floorNo;saveGame();}if(unlocked<MODS&&floorNo>=unlocked-1){unlocked++;saveGame();}if(floorNo>10){mode=WON;return;}makeChoices();mode=UPGRADE;}
static void installChoice(int idx){int m=choices[idx];if(hasMod(m)){status("Module already installed");return;}if(storageUsed+modSize[m]>MOD_BUDGET){status("NO SPACE - press M to delete a module");sound(80,100,1);return;}installed|=bit(m);recalc();sound(720,100,0);generate();mode=PLAY;}

static void playerStep(int dx,int dy){
  int nx=px+dx,ny=py+dy;Enemy *e;if(mode!=PLAY)return;e=enemyAt(nx,ny);if(e){hitEnemy(e,atk);enemyTurn();reveal();return;}if(!walkable(nx,ny)){sound(90,25,0);return;}px=nx;py=ny;
  if(map[py][px]==CHEST){map[py][px]=FLOOR;if(!hasKey){hasKey=1;status("KEY CHIP recovered");}else{gold+=8;hp++;if(hp>maxhp)hp=maxhp;status("Cache: +8 gold, +1 HP");}sound(840,80,0);}
  else if(map[py][px]==TRAP){map[py][px]=FLOOR;damagePlayer(2,0);status("BAD SECTOR! 2 damage");}
  else if(map[py][px]==EXIT){if(!hasKey)status("EXIT locked: KEY CHIP missing");else if(floorNo%5==0&&bossAlive())status("EXIT locked: destroy the BOSS");else{nextFloor();return;}}
  if(hasMod(4)&&irand(100)<15){status("DASH.EXE: bonus action");sound(620,35,0);}else enemyTurn();reveal();
}
static void fireball(void){
  int dx=0,dy=0,x,y,r;Enemy *e;if(!hasMod(7)){status("FIREBALL.BIN is not installed");return;}
  if(GetAsyncKeyState(VK_LEFT)&0x8000)dx=-1;else if(GetAsyncKeyState(VK_RIGHT)&0x8000)dx=1;else if(GetAsyncKeyState(VK_UP)&0x8000)dy=-1;else if(GetAsyncKeyState(VK_DOWN)&0x8000)dy=1;else{status("Hold an arrow + F to fire");return;}
  x=px;y=py;for(r=0;r<6;r++){x+=dx;y+=dy;if(map[y][x]==WALL)break;e=enemyAt(x,y);if(e){hitEnemy(e,atk+1);enemyTurn();reveal();sound(980,90,0);return;}}status("Bolt missed");enemyTurn();
}

static void drawSprite(HDC d,int x,int y,int t,int hpv){
  int X=x*TILE,Y=20+y*TILE,c=8;if(t==0)c=8;else if(t==1)c=4;else if(t==2)c=6;else if(t==3)c=9;else if(t==4)c=10;else if(t==5)c=11;else if(t==6)c=9;else c=7;
  if(t==3){rect(d,X+2,Y+1,6,6,c);rect(d,X+1,Y+4,8,3,c);}else if(t==4){rect(d,X+2,Y+2,6,6,c);rect(d,X+4,Y,2,2,7);}else if(t==7){rect(d,X+1,Y+1,8,8,c);rect(d,X+3,Y+3,2,2,0);rect(d,X+6,Y+3,2,2,0);}else{rect(d,X+2,Y+2,6,7,c);rect(d,X+3,Y+1,4,2,c);}if(hpv>0&&hpv<5)rect(d,X+1,Y,2*hpv,1,7);
}
static void drawMap(HDC d){
  int x,y,i,X,Y,sx=shake?irand(shake*2+1)-shake:0,sy=shake?irand(shake*2+1)-shake:0;SetViewportOrgEx(d,sx,sy,0);
  for(y=0;y<MH;y++)for(x=0;x<MW;x++){X=x*TILE;Y=20+y*TILE;if(!seen[y][x]){rect(d,X,Y,TILE,TILE,0);continue;}if(map[y][x]==WALL){rect(d,X,Y,TILE,TILE,2);rect(d,X+1,Y+1,8,8,1);}else{rect(d,X,Y,TILE,TILE,1);if((x+y)&1)rect(d,X+4,Y+4,1,1,2);if(map[y][x]==EXIT){rect(d,X+1,Y+1,8,8,hasKey?8:7);rect(d,X+3,Y+3,4,5,0);}else if(map[y][x]==CHEST){rect(d,X+2,Y+3,6,5,6);rect(d,X+4,Y+4,2,2,0);}else if(map[y][x]==TRAP){rect(d,X+2,Y+5,2,3,7);rect(d,X+6,Y+5,2,3,7);}}
  }
  for(i=0;i<enCount;i++)if(en[i].alive&&seen[en[i].y][en[i].x])drawSprite(d,en[i].x,en[i].y,en[i].type,en[i].hp);
  X=px*TILE;Y=20+py*TILE;rect(d,X+2,Y+1,6,8,5);rect(d,X+3,Y+2,1,1,0);rect(d,X+6,Y+2,1,1,0);rect(d,X+1,Y+5,8,2,5);
  for(i=0;i<MAX_FX;i++)if(fx[i].life>0)rect(d,fx[i].x+((i*3+animTick)%7)-3,fx[i].y+((i*5+animTick)%7)-3,2,2,fx[i].color);
  SetViewportOrgEx(d,0,0,0);
}
static void panel(HDC d){
  int y=24,i;rect(d,202,20,118,150,0);frame(d,203,21,115,147,2);SelectObject(d,fontSmall);
  txt(d,208,y,5,"FLOPPY DUNGEON");y+=13;txtInt(d,208,y,4,"FLOOR ",floorNo," / 10");y+=10;txtInt(d,208,y,hp<4?7:8,"HP ",hp,"/");txtInt(d,250,y,8,"",maxhp,"");y+=10;txtInt(d,208,y,6,"ATK ",atk,"   GOLD ");txtInt(d,274,y,6,"",gold,"");y+=10;txtInt(d,208,y,hasKey?8:7,"KEY ",hasKey,"/1");y+=13;
  txtInt(d,208,y,5,"DISK ",1280+storageUsed,"/1440K");y+=10;rect(d,208,y,100,5,2);rect(d,208,y,(storageUsed*100)/MOD_BUDGET,5,9);y+=10;
  txt(d,208,y,3,"LOADED MODULES");y+=9;SelectObject(d,fontTiny);
  for(i=0;i<MODS;i++)if(hasMod(i)){txt(d,210,y,9,modName[i]);y+=8;}
  SelectObject(d,fontTiny);txt(d,208,150,3,"M modules   H help");txt(d,208,159,3,"Arrows/WASD move");SelectObject(d,fontSmall);
}
static void overlay(HDC d,const char *title){rect(d,22,17,276,148,0);frame(d,22,17,276,148,5);frame(d,24,19,272,144,2);SelectObject(d,fontBig);txt(d,34,26,5,title);SelectObject(d,fontSmall);}

static void paint(HDC out){
  RECT cr;int ww,wh,scale,dw,dh,ox,oy,i,y;HBRUSH black=CreateSolidBrush(RGB(0,0,0));GetClientRect(win,&cr);FillRect(out,&cr,black);DeleteObject(black);rect(backdc,0,0,SW,SH,0);SelectObject(backdc,fontSmall);
  if(mode==TITLE){
    SelectObject(backdc,fontBig);txt(backdc,54,27,5,"1.44MB: THE LAST FLOPPY");SelectObject(backdc,fontSmall);txt(backdc,82,53,3,"A procedural turn-based roguelike");
    rect(backdc,38,70,62,52,2);rect(backdc,45,64,48,8,3);rect(backdc,48,82,28,22,0);frame(backdc,38,70,62,52,4);rect(backdc,82,78,10,10,6);rect(backdc,47,109,44,6,4);
    txt(backdc,124,72,8,"[ ENTER ]  NEW RUN");txt(backdc,124,89,6,"[ H ]      HOW TO PLAY");txt(backdc,124,106,9,"[ M ]      MODULE INDEX");txtInt(backdc,124,130,3,"BEST FLOOR: ",bestFloor,"");txt(backdc,38,151,3,"Game size is tiny. Your choices should be too.");
  }else if(mode==PLAY){drawMap(backdc);panel(backdc);rect(backdc,0,0,320,18,1);txt(backdc,5,5,4,messageTimer?message:"Recover the disk. Defeat the corruption.");}
  else if(mode==UPGRADE){drawMap(backdc);panel(backdc);overlay(backdc,"INSTALL ONE MODULE");txt(backdc,35,49,4,"A new sector was recovered. Choose 1-3:");for(i=0;i<3;i++){y=68+i*24;txtInt(backdc,37,y,6,"",i+1,".");txt(backdc,54,y,9,modName[choices[i]]);txtInt(backdc,196,y,6,"",modSize[choices[i]],"KB");txt(backdc,54,y+9,3,modDesc[choices[i]]);}txt(backdc,35,143,7,messageTimer?message:"M: uninstall modules    ESC: skip");}
  else if(mode==MODULES){overlay(backdc,"MODULE MANAGER");txtInt(backdc,35,47,4,"Capacity: ",storageUsed,"/160KB   toggle 1-8");SelectObject(backdc,fontTiny);for(i=0;i<MODS;i++){y=61+i*11;txtInt(backdc,35,y,i<unlocked?(hasMod(i)?8:4):2,"",i+1,".");txt(backdc,48,y,i<unlocked?(hasMod(i)?8:9):2,modName[i]);txtInt(backdc,128,y,6,"",modSize[i],"K");txt(backdc,160,y,3,modDesc[i]);}SelectObject(backdc,fontSmall);txt(backdc,35,151,5,"ESC: return    Green = installed");}
  else if(mode==HELP){overlay(backdc,"HOW TO PLAY");txt(backdc,35,51,4,"Arrows / WASD   Move or bump-attack");txt(backdc,35,64,4,"F + held arrow  Fire ranged module");txt(backdc,35,77,4,"M               Manage disk modules");txt(backdc,35,96,6,"Every move advances all enemies.");txt(backdc,35,109,6,"Find the yellow KEY CHIP, then EXIT.");txt(backdc,35,122,6,"Install abilities within 160KB free space.");txt(backdc,35,143,5,"ESC / H: return        R: restart run");}
  else if(mode==DEAD){overlay(backdc,"DISK FAILURE");txtInt(backdc,72,67,7,"Corruption reached you on floor ",floorNo,".");txtInt(backdc,72,85,6,"Gold recovered: ",gold,"");txt(backdc,72,112,8,"ENTER: initialize a new disk");txt(backdc,72,128,4,"ESC: return to title");}
  else {overlay(backdc,"SYSTEM RESTORED");txt(backdc,62,66,8,"The final corruption was deleted.");txtInt(backdc,62,84,6,"Gold recovered: ",gold,"");txt(backdc,62,111,5,"You saved an entire world in 1.44MB.");txt(backdc,62,133,4,"ENTER: play again    ESC: title");}
  GetClientRect(win,&cr);ww=cr.right;wh=cr.bottom;scale=ww/SW;if(wh/SH<scale)scale=wh/SH;if(scale<1)scale=1;dw=SW*scale;dh=SH*scale;ox=(ww-dw)/2;oy=(wh-dh)/2;SetStretchBltMode(out,COLORONCOLOR);StretchBlt(out,ox,oy,dw,dh,backdc,0,0,SW,SH,SRCCOPY);
}

static void toggleModule(int m){if(m<0||m>=unlocked)return;if(hasMod(m)){installed&=~bit(m);recalc();status("Module deleted; space recovered");sound(140,60,0);}else if(storageUsed+modSize[m]<=MOD_BUDGET){installed|=bit(m);recalc();status("Module installed");sound(700,60,0);}else{status("NO SPACE: delete another module first");sound(70,80,1);}}
static void keydown(WPARAM k){
  if(k=='H'){if(mode==HELP)mode=returnMode;else{returnMode=mode;mode=HELP;}InvalidateRect(win,0,FALSE);return;}
  if(k=='M'){if(mode==MODULES)mode=returnMode;else if(mode==TITLE){returnMode=TITLE;mode=MODULES;}else if(mode==PLAY||mode==UPGRADE){returnMode=mode;mode=MODULES;}InvalidateRect(win,0,FALSE);return;}
  if(k==VK_ESCAPE){if(mode==TITLE)PostMessageA(win,WM_CLOSE,0,0);else if(mode==MODULES||mode==HELP)mode=returnMode;else if(mode==UPGRADE){generate();mode=PLAY;}else mode=TITLE;InvalidateRect(win,0,FALSE);return;}
  if(mode==TITLE){if(k==VK_RETURN)beginRun();}
  else if(mode==DEAD||mode==WON){if(k==VK_RETURN)beginRun();}
  else if(mode==MODULES){if(k>='1'&&k<='8')toggleModule((int)(k-'1'));}
  else if(mode==UPGRADE){if(k>='1'&&k<='3')installChoice((int)(k-'1'));}
  else if(mode==PLAY){if(k==VK_LEFT||k=='A')playerStep(-1,0);else if(k==VK_RIGHT||k=='D')playerStep(1,0);else if(k==VK_UP||k=='W')playerStep(0,-1);else if(k==VK_DOWN||k=='S')playerStep(0,1);else if(k=='F')fireball();else if(k=='R')beginRun();}
  InvalidateRect(win,0,FALSE);
}

static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
  if(m==WM_KEYDOWN){keydown(w);return 0;}if(m==WM_ERASEBKGND)return 1;
  if(m==WM_TIMER){int i;animTick++;if(messageTimer)messageTimer--;if(shake)shake--;for(i=0;i<MAX_FX;i++)if(fx[i].life>0)fx[i].life--;InvalidateRect(h,0,FALSE);return 0;}
  if(m==WM_PAINT){PAINTSTRUCT p;HDC d=BeginPaint(h,&p);paint(d);EndPaint(h,&p);return 0;}
  if(m==WM_DESTROY){saveGame();PostQuitMessage(0);return 0;}return DefWindowProcA(h,m,w,l);
}

static int gameMain(HINSTANCE hi){
  WNDCLASSEXA wc;BITMAPINFO bi;MSG msg;RECT r={0,0,960,540};memset(&wc,0,sizeof(wc));wc.cbSize=sizeof(wc);wc.lpfnWndProc=proc;wc.hInstance=hi;wc.hCursor=LoadCursorA(0,IDC_ARROW);wc.hIcon=LoadIconA(0,IDI_APPLICATION);wc.lpszClassName="FloppyDungeon144";wc.style=CS_OWNDC;RegisterClassExA(&wc);
  AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);win=CreateWindowExA(0,wc.lpszClassName,"1.44MB: The Last Floppy",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,0,0,hi,0);
  backdc=CreateCompatibleDC(0);memset(&bi,0,sizeof(bi));bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=SW;bi.bmiHeader.biHeight=-SH;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;backbmp=CreateDIBSection(backdc,&bi,DIB_RGB_COLORS,(void**)&pixels,0,0);SelectObject(backdc,backbmp);
  fontSmall=CreateFontA(-9,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,FIXED_PITCH,"Terminal");fontTiny=CreateFontA(-8,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,FIXED_PITCH,"Terminal");fontBig=CreateFontA(-16,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,FIXED_PITCH,"Terminal");
  rng^=GetTickCount();loadGame();SetTimer(win,1,33,0);ShowWindow(win,SW_SHOW);UpdateWindow(win);
  while(GetMessageA(&msg,0,0,0)>0){TranslateMessage(&msg);DispatchMessageA(&msg);}DeleteObject(fontSmall);DeleteObject(fontTiny);DeleteObject(fontBig);DeleteObject(backbmp);DeleteDC(backdc);return (int)msg.wParam;
}

void WinMainCRTStartup(void){ExitProcess((UINT)gameMain(GetModuleHandleA(0)));}
