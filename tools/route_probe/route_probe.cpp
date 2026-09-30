#include "thor/session.hpp"
#include <iostream>
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
 try { thor::Session s(thor::Rom::from_file(argv[1])); std::vector<unsigned> LOG; auto S=[&](unsigned b){s.step(b);LOG.push_back(b);}; unsigned buttons,n;
 while (std::cin >> std::hex >> buttons >> std::dec >> n) {
  if(buttons==0xfffc) {
   for(unsigned jump=0;jump<n && (s.room().name=="Climb" || (s.room().name=="Parlor" && s.state().y/65536>180));jump++) {
    auto best=s;int score=s.state().y/65536;unsigned first=0,second=0,split=0,middle=0,prefix=0;
    for(unsigned a:{0u,0x100u,0x200u})for(unsigned b:{0u,0x100u,0x200u})for(unsigned pre:{0u,6u,12u,18u})for(unsigned t:{0u,12u,24u,36u,48u})for(unsigned u:{12u,24u,36u,48u}) { if(t+u>90)continue;
     auto trial=s;for(unsigned f=0;f<pre;f++)trial.step(a);
     for(unsigned f=0;f<90;f++)trial.step(0x8000|(f<t?a:f<t+u?b:0));
     for(unsigned f=0;f<40;f++)trial.step(0);
     if(trial.room().name!=s.room().name || !trial.state().grounded)continue;
     if(trial.state().y/65536<score) {score=trial.state().y/65536;best=std::move(trial);first=a;second=b;split=t;middle=u;prefix=pre;}
    }
    if(!middle){std::cout<<"ASCENT STUCK\n";break;}
    if(prefix)std::cout<<"WALK "<<std::hex<<first<<std::dec<<" "<<prefix<<"\n";
    std::cout<<"ASCEND "<<std::hex<<(0x8000|first)<<std::dec<<" "<<split<<" / "<<std::hex<<(0x8000|second)<<std::dec<<" "<<middle<<" / 8000 "<<(90-split-middle)<<" / 0 40\n";
    for(unsigned f=0;f<prefix;f++)S(first);
    for(unsigned f=0;f<90;f++)S(0x8000|(f<split?first:f<split+middle?second:0));
    for(unsigned f=0;f<40;f++)S(0);
   }
  } else if(buttons==0xfffd) {
   for(auto&e:s.enemies().list())std::cout<<"enemy kind="<<std::hex<<e.kind<<std::dec<<" health="<<e.health<<" x="<<e.x/65536.f<<" y="<<e.y/65536.f<<" active="<<e.active<<"\n";
   for(size_t i=0;i<s.room().doors.size();i++) {auto&d=s.room().doors[i];std::cout<<i<<" -> "<<std::hex<<d.destination<<std::dec<<" dir="<<int(d.direction)<<" cap="<<int(d.cap_x)<<","<<int(d.cap_y)<<" props="<<int(d.properties)<<"\n";}
   for(auto&i:s.gameplay().pickups())std::cout<<"item="<<i.item<<" x="<<(i.root%(s.room().width/16))*16+8<<" y="<<(i.root/(s.room().width/16))*16+8<<"\n";
   for(int y=0;y<s.room().height/16;y++)for(int x=0;x<s.room().width/16;x++){auto k=y*(s.room().width/16)+x;if((s.room().blocks[k]>>12)==12)std::cout<<"shotblock "<<x<<","<<y<<" bts="<<int(s.room().bts[k])<<"\n";}
   continue;
  }
  if (buttons == 0xfffe || buttons==0xfffb) {
   const int center=buttons==0xfffb?900:384;int target=center;
   for(unsigned i=0;i<n;i++) {
    const auto&a=s.state();int x=a.x/65536, y=a.y/65536;
    if (a.grounded) {
      int best=10000;
      for(int candidate=center-109;candidate<center+106;candidate+=4) {
       bool clear=true;for(int xx=candidate-5;xx<candidate+5;xx++)for(int yy=y-21;yy<y+46;yy++)if(thor::room_solid_pixel(s.room(),s.rom(),xx,yy))clear=false;
       if(clear && abs(candidate-x)+abs(candidate-center)/4<best){best=abs(candidate-x)+abs(candidate-center)/4;target=candidate;}
      }
    }
    S((x<target?thor::Right:x>target?thor::Left:0)|thor::Shoot);
    if(s.room().name!="Parlor" && s.room().name!="Climb")break;
   }
  } else if(buttons==0xfffa) {
   int px=s.state().x/65536/16,py=s.state().y/65536/16;
   for(int y=py-int(n);y<=py+int(n);y++){std::cout<<y<<'\t';for(int x=px-30;x<=px+30;x++){ if(x<0||y<0||x>=s.room().width/16||y>=s.room().height/16){std::cout<<' ';continue;} auto b=s.room().block(x,y)>>12; char c=b==0?'.':b==1?'/':b==5||b==13?'e':b==9?'T':b==12?'D':b==11?'s':b>=8?'#':'.'; if(x==px&&(y==py||y==py+1)) c='@'; std::cout<<c;}std::cout<<'\n';}
   std::cout<<"px="<<px<<" py="<<py<<'\n';
   continue;
  } else if(buttons==0xfff7) {
   for(auto&i:s.gameplay().pickups())std::cout<<"pickup item="<<i.item<<" group="<<i.group<<" root="<<i.root<<" rev="<<i.revealed<<" taken="<<i.taken<<" arg="<<i.argument<<"\n";
   std::cout<<"eq="<<s.progression().equipped_items<<" col="<<s.progression().collected_items<<"\n";
   continue;
  } else if(buttons==0xfff6) {
   for(unsigned f=0;f<n;f++){
     unsigned b=thor::Shoot; const auto&E=s.enemies().list();
     if(!E.empty()&&E[0].active){ int dx=E[0].x/65536-s.state().x/65536; int p=s.state().pose;
       if(dx<-8&&p==1) b|=thor::Left; else if(dx>8&&p==2) b|=thor::Right;
       if(f%90>60) b|=thor::Jump; }
     S(b);
     if(s.state().dead) {std::cout<<"DEAD at "<<f<<"\n";break;}
   }
  } else if(buttons==0xffff) {
   for(int y=0;y<s.room().height/16;y++) {std::cout<<y<<" ";for(int x=0;x<s.room().width/16;x++) {auto b=s.room().block(x,y)>>12; char c=b==0?'.':b==1?'/':b==5||b==13?'e':b==9?'T':b==12?'D':b==11?'s':b>=8?'#':'.';std::cout<<c;}std::cout<<"\n";} continue;
  }
  if(buttons!=0xfffe && buttons!=0xfffc && buttons!=0xfffb && buttons!=0xfff6) for (unsigned i=0;i<n;++i) S(buttons);
  auto a=s.state(); std::cout<<s.room().name<<" x="<<a.x/65536.f<<" y="<<a.y/65536.f<<" cx="<<a.camera_x<<" cy="<<a.camera_y<<" pose="<<std::hex<<a.pose<<std::dec<<" grounded="<<a.grounded<<" transition="<<int(a.transition)<<" health="<<s.progression().health<<" items="<<s.progression().collected_items<<" missiles="<<s.progression().missiles<<"\n";
 }
 if(const char*out=getenv("ROUTE_OUT")){FILE*f=fopen(out,"w");for(size_t i=0;i<LOG.size();){size_t j=i;while(j<LOG.size()&&LOG[j]==LOG[i])j++;fprintf(f,"%x %zu\n",LOG[i],j-i);i=j;}fclose(f);}
 } catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
}
