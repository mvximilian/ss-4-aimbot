#pragma once
enum class Priority {Closest=0,Fov=1,Farthest=2,LowestHealth=3};
struct Rank {float angle,distance;int health;uintptr_t address;};
bool prefer(Priority mode,Rank a,Rank b){
    if(!b.address)return true;
    float x=0,y=0;
    switch(mode){
    case Priority::Closest:x=a.distance;y=b.distance;break;
    case Priority::Farthest:x=-a.distance;y=-b.distance;break;
    case Priority::LowestHealth:x=(float)a.health;y=(float)b.health;break;
    default:x=a.angle;y=b.angle;break;
    }
    if(x!=y)return x<y;
    if(a.angle!=b.angle)return a.angle<b.angle;
    if(a.distance!=b.distance)return a.distance<b.distance;
    return a.address<b.address;
}
