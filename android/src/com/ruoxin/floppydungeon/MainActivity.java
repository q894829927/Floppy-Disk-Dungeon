package com.ruoxin.floppydungeon;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.media.AudioManager;
import android.media.ToneGenerator;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

public final class MainActivity extends Activity {
    private GameView game;

    @Override public void onCreate(Bundle b) {
        super.onCreate(b);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        game = new GameView(this);
        setContentView(game);
        immersive();
    }

    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) immersive();
    }

    @Override protected void onPause() {
        super.onPause();
        if (game != null) game.save();
    }

    @Override public boolean onKeyDown(int code, KeyEvent e) {
        if (game != null && game.key(code)) return true;
        return super.onKeyDown(code, e);
    }

    static final class GameView extends View {
        static final int MW=20, MH=15, TILE=10, VW=360, VH=200;
        static final int WALL=0, FLOOR=1, EXIT=2, CHEST=3, TRAP=4;
        static final int TITLE=0, PLAY=1, UPGRADE=2, MODULES=3, HELP=4, DEAD=5, WON=6;
        static final int MODS=8, MOD_BUDGET=160, MAX_EN=36;

        static final int[] PAL={
                Color.rgb(7,10,20), Color.rgb(24,34,52), Color.rgb(44,62,80),
                Color.rgb(92,110,124), Color.rgb(214,223,230), Color.rgb(57,184,255),
                Color.rgb(255,205,65), Color.rgb(255,76,96), Color.rgb(76,220,128),
                Color.rgb(181,103,255), Color.rgb(255,135,50), Color.rgb(60,120,190)
        };
        static final String[] MOD_NAME={"PATCH.HP","BLADE.SYS","SHIELD.DRV","LEECH.DLL","DASH.EXE","SCAN.COM","THORNS.SYS","FIREBALL.BIN"};
        static final String[] MOD_DESC={"Max HP +3","Attack +1","Block first hit","Heal after 3 kills","15% extra turn","Reveal whole map","Return 1 damage","Ranged bolt"};
        static final int[] MOD_SIZE={48,40,24,40,32,48,32,56};

        final Paint p=new Paint();
        final int[][] map=new int[MH][MW];
        final boolean[][] seen=new boolean[MH][MW];
        final Enemy[] enemies=new Enemy[MAX_EN];
        final int[] rx=new int[9], ry=new int[9], rw=new int[9], rh=new int[9];
        final int[] choices=new int[3];
        final ToneGenerator tone;
        final SharedPreferences prefs;

        int seed=(int)System.nanoTime(), mode=TITLE, returnMode=PLAY, roomCount, enemyCount;
        int px,py,hp,maxHp,attack,defense,gold,floor,kills,turn,hasKey;
        int installed,unlocked,storageUsed,shieldReady,bestFloor,messageTicks,flashTicks;
        boolean fireMode;
        String message="";
        float scale, offsetX, offsetY;

        GameView(Context c) {
            super(c);
            setFocusable(true);
            setKeepScreenOn(true);
            p.setAntiAlias(false);
            p.setTypeface(Typeface.MONOSPACE);
            tone=new ToneGenerator(AudioManager.STREAM_MUSIC,45);
            prefs=c.getSharedPreferences("floppy",Context.MODE_PRIVATE);
            bestFloor=prefs.getInt("best",0);
            unlocked=prefs.getInt("unlocked",3);
            if(unlocked<3)unlocked=3;
            if(unlocked>MODS)unlocked=MODS;
            for(int i=0;i<MAX_EN;i++)enemies[i]=new Enemy();
        }

        int random(int n) {
            seed^=seed<<13; seed^=seed>>>17; seed^=seed<<5;
            return n==0?0:(seed&0x7fffffff)%n;
        }
        int abs(int n){return n<0?-n:n;}
        int distance(int x,int y,int a,int b){return abs(x-a)+abs(y-b);}
        boolean hasMod(int n){return (installed&(1<<n))!=0;}
        void say(String s){message=s;messageTicks=100;}
        void beep(int kind){
            int t=ToneGenerator.TONE_PROP_BEEP;
            if(kind==1)t=ToneGenerator.TONE_PROP_ACK;
            else if(kind==2)t=ToneGenerator.TONE_PROP_NACK;
            else if(kind==3)t=ToneGenerator.TONE_CDMA_ALERT_CALL_GUARD;
            tone.startTone(t,kind==3?140:65);
        }

        @Override protected void onDraw(Canvas c) {
            super.onDraw(c);
            c.drawColor(Color.BLACK);
            scale=Math.min(getWidth()/(float)VW,getHeight()/(float)VH);
            offsetX=(getWidth()-VW*scale)/2f;
            offsetY=(getHeight()-VH*scale)/2f;
            c.save(); c.translate(offsetX,offsetY); c.scale(scale,scale);
            fill(c,0,0,VW,VH,0);
            if(mode==TITLE)drawTitle(c);
            else if(mode==PLAY)drawGame(c);
            else if(mode==UPGRADE)drawUpgrade(c);
            else if(mode==MODULES)drawModules(c);
            else if(mode==HELP)drawHelp(c);
            else if(mode==DEAD)drawEnd(c,false);
            else drawEnd(c,true);
            if(flashTicks>0){p.setColor(Color.argb(flashTicks*10,255,70,80));c.drawRect(0,0,VW,VH,p);flashTicks--;}
            c.restore();
            if(messageTicks>0)messageTicks--;
            postInvalidateDelayed(33);
        }

        void fill(Canvas c,float x,float y,float w,float h,int color){p.setStyle(Paint.Style.FILL);p.setColor(PAL[color%PAL.length]);c.drawRect(x,y,x+w,y+h,p);}
        void border(Canvas c,float x,float y,float w,float h,int color){p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1);p.setColor(PAL[color%PAL.length]);c.drawRect(x+.5f,y+.5f,x+w-.5f,y+h-.5f,p);p.setStyle(Paint.Style.FILL);}
        void text(Canvas c,String s,float x,float y,int color,float size){p.setColor(PAL[color%PAL.length]);p.setTextSize(size);p.setTypeface(Typeface.MONOSPACE);p.setStyle(Paint.Style.FILL);c.drawText(s,x,y,p);}

        void drawTitle(Canvas c){
            text(c,"1.44MB: THE LAST FLOPPY",43,38,5,16);
            text(c,"A pocket-sized turn-based roguelike",66,57,3,8);
            fill(c,48,76,64,55,2);border(c,48,76,64,55,4);fill(c,56,69,48,8,3);
            fill(c,60,88,30,23,0);fill(c,94,84,10,10,6);fill(c,57,118,46,6,4);
            button(c,145,75,160,25,"TAP TO START",8);
            button(c,145,108,160,25,"HOW TO PLAY",6);
            text(c,"BEST FLOOR: "+bestFloor,145,151,3,9);
            text(c,"Tiny game. Expensive choices.",71,181,3,8);
        }

        void drawGame(Canvas c){
            fill(c,0,0,VW,18,1);
            text(c,messageTicks>0?message:"Recover the disk. Delete corruption.",5,13,4,8);
            drawMap(c); drawPanel(c); drawControls(c);
        }

        void drawMap(Canvas c){
            for(int y=0;y<MH;y++)for(int x=0;x<MW;x++){
                int X=x*TILE,Y=20+y*TILE;
                if(!seen[y][x]){fill(c,X,Y,TILE,TILE,0);continue;}
                if(map[y][x]==WALL){fill(c,X,Y,TILE,TILE,2);fill(c,X+1,Y+1,8,8,1);}
                else{
                    fill(c,X,Y,TILE,TILE,1);
                    if(((x+y)&1)!=0)fill(c,X+4,Y+4,1,1,2);
                    if(map[y][x]==EXIT){fill(c,X+1,Y+1,8,8,hasKey!=0?8:7);fill(c,X+3,Y+3,4,5,0);}
                    else if(map[y][x]==CHEST){fill(c,X+2,Y+3,6,5,6);fill(c,X+4,Y+4,2,2,0);}
                    else if(map[y][x]==TRAP){fill(c,X+2,Y+5,2,3,7);fill(c,X+6,Y+5,2,3,7);}
                }
            }
            for(int i=0;i<enemyCount;i++)if(enemies[i].alive&&seen[enemies[i].y][enemies[i].x])drawEnemy(c,enemies[i]);
            int X=px*TILE,Y=20+py*TILE;
            fill(c,X+2,Y+1,6,8,5);fill(c,X+3,Y+2,1,1,0);fill(c,X+6,Y+2,1,1,0);fill(c,X+1,Y+5,8,2,5);
        }

        void drawEnemy(Canvas c,Enemy e){
            int X=e.x*TILE,Y=20+e.y*TILE,color=8;
            if(e.type==1)color=4;else if(e.type==2)color=6;else if(e.type==3)color=9;
            else if(e.type==4)color=10;else if(e.type==5)color=11;else if(e.type==6)color=9;else if(e.type==7)color=7;
            if(e.type==3){fill(c,X+2,Y+1,6,6,color);fill(c,X+1,Y+4,8,3,color);}
            else if(e.type==4){fill(c,X+2,Y+2,6,6,color);fill(c,X+4,Y,2,2,7);}
            else if(e.type==7){fill(c,X+1,Y+1,8,8,color);fill(c,X+3,Y+3,2,2,0);fill(c,X+6,Y+3,2,2,0);}
            else{fill(c,X+2,Y+2,6,7,color);fill(c,X+3,Y+1,4,2,color);}
            if(e.hp<5)fill(c,X+1,Y,Math.max(1,e.hp*2),1,7);
        }

        void drawPanel(Canvas c){
            fill(c,202,20,158,180,0);border(c,203,21,156,178,2);
            text(c,"FLOPPY DUNGEON",209,33,5,9);
            text(c,"FLOOR "+floor+" / 10",209,47,4,8);
            text(c,"HP "+hp+"/"+maxHp,209,59,hp<4?7:8,8);
            text(c,"ATK "+attack+"  GOLD "+gold,209,71,6,8);
            text(c,"KEY "+hasKey+"/1",209,83,hasKey!=0?8:7,8);
            text(c,"DISK "+(1280+storageUsed)+"/1440K",209,96,5,8);
            fill(c,209,101,136,5,2);fill(c,209,101,storageUsed*136/MOD_BUDGET,5,9);
            text(c,"LOADED:",209,117,3,7);
            int y=128;
            for(int i=0;i<MODS&&y<148;i++)if(hasMod(i)){text(c,MOD_NAME[i],209,y,9,6);y+=7;}
        }

        void drawControls(Canvas c){
            touchBox(c,215,153,20,18,"<",4);
            touchBox(c,237,134,20,18,"^",4);
            touchBox(c,237,172,20,18,"v",4);
            touchBox(c,259,153,20,18,">",4);
            touchBox(c,290,145,28,28,fireMode?"AIM":"FIRE",fireMode?7:6);
            touchBox(c,324,145,28,28,"MOD",9);
            touchBox(c,290,177,62,16,"HELP",3);
        }

        void touchBox(Canvas c,int x,int y,int w,int h,String label,int color){fill(c,x,y,w,h,1);border(c,x,y,w,h,color);float tw=p.measureText(label);text(c,label,x+w/2f-tw/2f,y+h/2f+3,color,label.length()>2?6:9);}
        void button(Canvas c,int x,int y,int w,int h,String label,int color){fill(c,x,y,w,h,1);border(c,x,y,w,h,color);text(c,label,x+12,y+17,color,10);}
        void overlay(Canvas c,String title){fill(c,28,17,304,166,0);border(c,28,17,304,166,5);border(c,30,19,300,162,2);text(c,title,43,45,5,15);}

        void drawUpgrade(Canvas c){
            drawGame(c);overlay(c,"INSTALL ONE MODULE");
            text(c,"Recovered sector: choose one",43,62,4,8);
            for(int i=0;i<3;i++){
                int y=70+i*28;fill(c,41,y,278,24,1);border(c,41,y,278,24,6);
                text(c,(i+1)+". "+MOD_NAME[choices[i]],49,y+10,9,8);
                text(c,MOD_SIZE[choices[i]]+"KB  "+MOD_DESC[choices[i]],49,y+20,3,7);
            }
            text(c,messageTicks>0?message:"Tap an option, or MOD to manage space",43,170,7,7);
        }

        void drawModules(Canvas c){
            overlay(c,"MODULE MANAGER");
            text(c,"Capacity: "+storageUsed+"/160KB  tap to install/delete",43,61,4,8);
            for(int i=0;i<MODS;i++){
                int y=68+i*12;int color=i>=unlocked?2:(hasMod(i)?8:9);
                fill(c,41,y,278,11,1);border(c,41,y,278,11,color);
                text(c,(i+1)+". "+MOD_NAME[i],47,y+8,color,7);
                text(c,MOD_SIZE[i]+"K",139,y+8,6,7);
                text(c,MOD_DESC[i],174,y+8,3,6);
            }
            text(c,"Tap outside the list to return",43,177,5,7);
        }

        void drawHelp(Canvas c){
            overlay(c,"HOW TO PLAY");
            text(c,"D-pad        Move or bump-attack",43,67,4,8);
            text(c,"FIRE + D-pad Shoot FIREBALL.BIN",43,82,4,8);
            text(c,"MOD          Manage disk modules",43,97,4,8);
            text(c,"Every move advances all enemies.",43,118,6,8);
            text(c,"Find the KEY CHIP, then reach EXIT.",43,133,6,8);
            text(c,"Abilities share only 160KB free space.",43,148,6,8);
            text(c,"Tap anywhere to return",43,173,5,8);
        }

        void drawEnd(Canvas c,boolean win){
            overlay(c,win?"SYSTEM RESTORED":"DISK FAILURE");
            text(c,win?"The final corruption was deleted.":"Corruption reached floor "+floor+".",58,79,win?8:7,9);
            text(c,"Gold recovered: "+gold,58,99,6,9);
            text(c,win?"A whole world survived in 1.44MB.":"The damaged sector can be rebuilt.",58,122,4,8);
            button(c,58,141,244,25,"TAP TO INITIALIZE",8);
        }

        boolean onBox(float x,float y,int bx,int by,int bw,int bh){return x>=bx&&x<bx+bw&&y>=by&&y<by+bh;}
        @Override public boolean onTouchEvent(MotionEvent e){
            if(e.getAction()!=MotionEvent.ACTION_DOWN)return true;
            float x=(e.getX()-offsetX)/scale,y=(e.getY()-offsetY)/scale;
            if(x<0||y<0||x>=VW||y>=VH)return true;
            if(mode==TITLE){if(onBox(x,y,145,75,160,25))startRun();else if(onBox(x,y,145,108,160,25)){returnMode=TITLE;mode=HELP;}}
            else if(mode==HELP){mode=returnMode;}
            else if(mode==DEAD||mode==WON){startRun();}
            else if(mode==MODULES){
                boolean hit=false;
                for(int i=0;i<MODS;i++)if(onBox(x,y,41,68+i*12,278,11)){toggleModule(i);hit=true;break;}
                if(!hit)mode=returnMode;
            } else if(mode==UPGRADE){
                boolean hit=false;
                for(int i=0;i<3;i++)if(onBox(x,y,41,70+i*28,278,24)){installChoice(i);hit=true;break;}
                if(!hit&&x>300){returnMode=UPGRADE;mode=MODULES;}
            } else if(mode==PLAY){
                if(onBox(x,y,215,153,20,18))direction(-1,0);
                else if(onBox(x,y,237,134,20,18))direction(0,-1);
                else if(onBox(x,y,237,172,20,18))direction(0,1);
                else if(onBox(x,y,259,153,20,18))direction(1,0);
                else if(onBox(x,y,290,145,28,28)){fireMode=!fireMode;say(fireMode?"FIRE AIM: choose a direction":"FIRE AIM cancelled");}
                else if(onBox(x,y,324,145,28,28)){returnMode=PLAY;mode=MODULES;}
                else if(onBox(x,y,290,177,62,16)){returnMode=PLAY;mode=HELP;}
            }
            invalidate();return true;
        }

        void direction(int dx,int dy){if(fireMode){fireMode=false;fireball(dx,dy);}else playerStep(dx,dy);}
        boolean key(int code){
            if(code==KeyEvent.KEYCODE_H){if(mode==HELP)mode=returnMode;else{returnMode=mode;mode=HELP;}invalidate();return true;}
            if(code==KeyEvent.KEYCODE_M){if(mode==MODULES)mode=returnMode;else{returnMode=mode;mode=MODULES;}invalidate();return true;}
            if(code==KeyEvent.KEYCODE_BACK){if(mode==TITLE)return false;if(mode==HELP||mode==MODULES)mode=returnMode;else mode=TITLE;invalidate();return true;}
            if(mode==TITLE&&(code==KeyEvent.KEYCODE_ENTER||code==KeyEvent.KEYCODE_SPACE)){startRun();return true;}
            if(mode==DEAD||mode==WON){startRun();return true;}
            if(mode==PLAY){
                if(code==KeyEvent.KEYCODE_DPAD_LEFT||code==KeyEvent.KEYCODE_A)direction(-1,0);
                else if(code==KeyEvent.KEYCODE_DPAD_RIGHT||code==KeyEvent.KEYCODE_D)direction(1,0);
                else if(code==KeyEvent.KEYCODE_DPAD_UP||code==KeyEvent.KEYCODE_W)direction(0,-1);
                else if(code==KeyEvent.KEYCODE_DPAD_DOWN||code==KeyEvent.KEYCODE_S)direction(0,1);
                else if(code==KeyEvent.KEYCODE_F){fireMode=!fireMode;say("FIRE AIM: choose a direction");}
                else return false;return true;
            }
            return false;
        }

        void startRun(){hp=maxHp=10;attack=2;defense=0;gold=0;floor=1;kills=0;turn=0;installed=0;storageUsed=0;hasKey=0;fireMode=false;mode=PLAY;generate();}
        void save(){prefs.edit().putInt("best",bestFloor).putInt("unlocked",unlocked).apply();}

        void recalc(){
            storageUsed=0;maxHp=10;attack=2;defense=0;
            for(int i=0;i<MODS;i++)if(hasMod(i))storageUsed+=MOD_SIZE[i];
            if(hasMod(0))maxHp+=3;if(hasMod(1))attack++;
            if(hp>maxHp)hp=maxHp;reveal();
        }
        void toggleModule(int m){
            if(m<0||m>=unlocked)return;
            if(hasMod(m)){installed&=~(1<<m);recalc();say("Module deleted; space recovered");beep(0);}
            else if(storageUsed+MOD_SIZE[m]<=MOD_BUDGET){installed|=1<<m;recalc();say("Module installed");beep(1);}
            else{say("NO SPACE: delete another module");beep(2);}
        }

        boolean walkable(int x,int y){return x>0&&y>0&&x<MW-1&&y<MH-1&&map[y][x]!=WALL;}
        Enemy enemyAt(int x,int y){for(int i=0;i<enemyCount;i++){Enemy e=enemies[i];if(e.alive&&e.x==x&&e.y==y)return e;}return null;}
        boolean bossAlive(){for(int i=0;i<enemyCount;i++)if(enemies[i].alive&&enemies[i].type==7)return true;return false;}
        void reveal(){for(int y=0;y<MH;y++)for(int x=0;x<MW;x++)if(hasMod(5)||distance(px,py,x,y)<5)seen[y][x]=true;}

        void damagePlayer(int amount,Enemy source){
            if(hasMod(2)&&shieldReady!=0){shieldReady=0;say("SHIELD.DRV blocked damage");beep(0);return;}
            amount-=defense;if(amount<1)amount=1;hp-=amount;flashTicks=8;beep(2);
            if(source!=null&&hasMod(6)&&source.alive){source.hp--;if(source.hp<=0)killEnemy(source);}
            if(hp<=0){hp=0;mode=DEAD;if(floor>bestFloor){bestFloor=floor;save();}}
        }
        void killEnemy(Enemy e){e.alive=false;kills++;gold+=1+random(4);beep(1);if(hasMod(3)&&kills%3==0&&hp<maxHp){hp++;say("LEECH.DLL recovered 1 HP");}}
        void hitEnemy(Enemy e,int power){if(e.hp<=power)killEnemy(e);else{e.hp-=Math.max(1,power);beep(0);}}

        void playerStep(int dx,int dy){
            if(mode!=PLAY)return;int nx=px+dx,ny=py+dy;Enemy e=enemyAt(nx,ny);
            if(e!=null){hitEnemy(e,attack);enemyTurn();reveal();return;}
            if(!walkable(nx,ny)){beep(0);return;}px=nx;py=ny;
            if(map[py][px]==CHEST){map[py][px]=FLOOR;if(hasKey==0){hasKey=1;say("KEY CHIP recovered");}else{gold+=8;hp=Math.min(maxHp,hp+1);say("Cache: +8 gold, +1 HP");}beep(1);}
            else if(map[py][px]==TRAP){map[py][px]=FLOOR;damagePlayer(2,null);say("BAD SECTOR! 2 damage");}
            else if(map[py][px]==EXIT){if(hasKey==0)say("EXIT locked: KEY CHIP missing");else if(floor%5==0&&bossAlive())say("EXIT locked: destroy the BOSS");else{nextFloor();return;}}
            if(hasMod(4)&&random(100)<15){say("DASH.EXE: bonus action");beep(1);}else enemyTurn();reveal();
        }

        void fireball(int dx,int dy){
            if(!hasMod(7)){say("FIREBALL.BIN is not installed");beep(2);return;}
            int x=px,y=py;
            for(int r=0;r<6;r++){x+=dx;y+=dy;if(x<0||y<0||x>=MW||y>=MH||map[y][x]==WALL)break;Enemy e=enemyAt(x,y);if(e!=null){hitEnemy(e,attack+1);enemyTurn();reveal();beep(1);return;}}
            say("Bolt missed");enemyTurn();reveal();
        }

        boolean lineClear(int x,int y,int tx,int ty){
            int dx=Integer.compare(tx,x),dy=Integer.compare(ty,y);if(dx!=0&&dy!=0)return false;x+=dx;y+=dy;
            while(x!=tx||y!=ty){if(map[y][x]==WALL)return false;x+=dx;y+=dy;}return true;
        }
        void enemyMove(Enemy e,int dx,int dy,boolean ghost){
            int nx=e.x+dx,ny=e.y+dy;if(nx==px&&ny==py){damagePlayer(e.type==7?3:1,e);return;}
            if((ghost||walkable(nx,ny))&&enemyAt(nx,ny)==null){e.x=nx;e.y=ny;}
        }
        void chase(Enemy e,boolean away,boolean ghost){
            int dx=Integer.compare(px,e.x),dy=Integer.compare(py,e.y);if(away){dx=-dx;dy=-dy;}
            if(random(2)!=0){if(dy!=0)enemyMove(e,0,dy,ghost);else enemyMove(e,dx,0,ghost);}
            else{if(dx!=0)enemyMove(e,dx,0,ghost);else enemyMove(e,0,dy,ghost);}
        }

        void enemyTurn(){
            turn++;
            int count=enemyCount;
            for(int i=0;i<count&&hp>0;i++){
                Enemy e=enemies[i];if(!e.alive)continue;int d=distance(e.x,e.y,px,py);e.turn++;
                if(e.type==0){if((e.turn&1)==0)chase(e,false,false);}
                else if(e.type==1)chase(e,false,false);
                else if(e.type==2){if((e.x==px||e.y==py)&&d<=6&&lineClear(e.x,e.y,px,py))damagePlayer(1,e);else chase(e,d<3,false);}
                else if(e.type==3)chase(e,false,true);
                else if(e.type==4){if(e.armed){if(d<=2)damagePlayer(3,e);e.alive=false;beep(3);}else if(d<=2){e.armed=true;say("BOMB armed: move away!");}else chase(e,false,false);}
                else if(e.type==5){if(d<5)chase(e,false,false);else enemyMove(e,random(3)-1,random(3)-1,false);}
                else if(e.type==6){if(e.turn%4==0){int sx=e.x+random(3)-1,sy=e.y+random(3)-1;if(walkable(sx,sy))spawnEnemy(sx,sy,0);}else if(d<6&&lineClear(e.x,e.y,px,py))damagePlayer(1,e);else chase(e,false,false);}
                else{if(e.hp<e.maxHp/2&&e.turn%3==0){for(int a=0;a<4;a++){int sx=e.x+(a==0?1:a==1?-1:0),sy=e.y+(a==2?1:a==3?-1:0);if(walkable(sx,sy))spawnEnemy(sx,sy,1);}}else if(d<=1)damagePlayer(3,e);else chase(e,false,false);}
            }
        }

        void spawnEnemy(int x,int y,int type){
            if(enemyCount>=MAX_EN||enemyAt(x,y)!=null||(x==px&&y==py))return;
            Enemy e=enemies[enemyCount++];e.x=x;e.y=y;e.type=type;e.maxHp=e.hp=type==7?16+floor*2:2+floor/2+(type==4?1:0);e.turn=0;e.armed=false;e.alive=true;
        }
        void carve(int x,int y,int w,int h){for(int b=y;b<y+h;b++)for(int a=x;a<x+w;a++)map[b][a]=FLOOR;}
        void corridor(int x,int y,int tx,int ty){while(x!=tx){map[y][x]=FLOOR;x+=tx>x?1:-1;}while(y!=ty){map[y][x]=FLOOR;y+=ty>y?1:-1;}map[y][x]=FLOOR;}
        boolean floorSpot(int[] out){for(int tries=0;tries<300;tries++){int x=1+random(MW-2),y=1+random(MH-2);if(map[y][x]==FLOOR&&enemyAt(x,y)==null&&distance(px,py,x,y)>4){out[0]=x;out[1]=y;return true;}}return false;}

        void generate(){
            for(int y=0;y<MH;y++)for(int x=0;x<MW;x++){map[y][x]=WALL;seen[y][x]=false;}
            enemyCount=0;roomCount=0;int target=6+random(3);
            for(int tries=0;tries<180&&roomCount<target;tries++){
                int w=3+random(5),h=3+random(4),x=1+random(MW-w-2),y=1+random(MH-h-2);boolean ok=true;
                for(int j=0;j<roomCount;j++)if(x<rx[j]+rw[j]+1&&x+w+1>rx[j]&&y<ry[j]+rh[j]+1&&y+h+1>ry[j])ok=false;
                if(ok){rx[roomCount]=x;ry[roomCount]=y;rw[roomCount]=w;rh[roomCount]=h;carve(x,y,w,h);if(roomCount>0)corridor(rx[roomCount-1]+rw[roomCount-1]/2,ry[roomCount-1]+rh[roomCount-1]/2,x+w/2,y+h/2);roomCount++;}
            }
            if(roomCount<2){carve(1,1,MW-2,MH-2);rx[0]=ry[0]=1;rw[0]=rh[0]=5;rx[1]=MW-6;ry[1]=MH-6;rw[1]=rh[1]=5;roomCount=2;}
            px=rx[0]+rw[0]/2;py=ry[0]+rh[0]/2;
            int ex=rx[roomCount-1]+rw[roomCount-1]/2,ey=ry[roomCount-1]+rh[roomCount-1]/2;map[ey][ex]=EXIT;
            hasKey=0;int[] spot=new int[2];if(floorSpot(spot))map[spot[1]][spot[0]]=CHEST;
            int count=Math.min(24,4+floor*2);
            for(int i=0;i<count;i++)if(floorSpot(spot))spawnEnemy(spot[0],spot[1],floor%5==0&&i==0?7:random(floor<3?3:7));
            for(int i=0;i<2+floor/3;i++)if(floorSpot(spot))map[spot[1]][spot[0]]=TRAP;
            shieldReady=1;reveal();say("Find the KEY CHIP, then reach EXIT");
        }

        void nextFloor(){
            floor++;if(floor>bestFloor){bestFloor=floor;save();}
            if(unlocked<MODS&&floor>=unlocked-1){unlocked++;save();}
            if(floor>10){mode=WON;return;}makeChoices();mode=UPGRADE;
        }
        void makeChoices(){
            for(int i=0;i<3;i++){int guard=0;do{choices[i]=random(unlocked);guard++;}while(guard<40&&((i>0&&choices[i]==choices[0])||(i>1&&choices[i]==choices[1])));}
        }
        void installChoice(int i){
            int m=choices[i];if(hasMod(m)){say("Module already installed");return;}
            if(storageUsed+MOD_SIZE[m]>MOD_BUDGET){say("NO SPACE: open MOD manager");beep(2);return;}
            installed|=1<<m;recalc();beep(1);generate();mode=PLAY;
        }

        static final class Enemy {int x,y,type,hp,maxHp,turn;boolean armed,alive;}
    }
}
