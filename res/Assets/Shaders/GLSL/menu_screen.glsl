// DARK DREAM — Optimized + Vinheta controlável
// Altere este valor entre 0.0 e 1.0:
#define VIGNETTE_EXTRA 0.0

float hash(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}

float noise2D(vec2 p){
    vec2 i=floor(p),f=fract(p);
    vec2 u=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),u.x),
               mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),u.x),u.y);
}

float fbm(vec2 p){
    float v=0.0,a=0.5;
    for(int i=0;i<4;i++){v+=a*noise2D(p);p*=2.0;a*=0.5;}
    return v;
}

mat2 rot2(float a){float s=sin(a),c=cos(a);return mat2(c,-s,s,c);}

vec2 warp(vec2 p){
    float t=iTime*0.12;
    vec2 q=vec2(fbm(p*1.6+vec2(1.7,9.2)+t),
                fbm(p*1.4+vec2(8.3,2.8)-t));
    p+=0.45*(q-0.5);
    p*=rot2(iTime*0.04);
    return p;
}

vec3 palette(float t){
    vec3 a=vec3(0.42,0.38,0.45),b=vec3(0.55,0.45,0.60);
    vec3 c=vec3(1.0),d=vec3(0.08,0.42,0.32);
    return a+b*cos(6.28318*(c*t+d));
}

float hash3(vec3 p){return fract(sin(dot(p,vec3(12.9898,78.233,45.164)))*43758.5453);}

float smoothNoise3D(vec3 p){
    vec3 i=floor(p),f=fract(p);
    f=f*f*(3.0-2.0*f);
    return mix(
        mix(mix(hash3(i),hash3(i+vec3(1,0,0)),f.x),
            mix(hash3(i+vec3(0,1,0)),hash3(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hash3(i+vec3(0,0,1)),hash3(i+vec3(1,0,1)),f.x),
            mix(hash3(i+vec3(0,1,1)),hash3(i+vec3(1,1,1)),f.x),f.y),
        f.z);
}

float bayer4(vec2 pix){
    vec2 p = mod(floor(pix), 4.0);
    float x0 = step(1.0, p.x);
    float y0 = step(1.0, p.y);
    float x1 = step(2.0, p.x);
    float y1 = step(2.0, p.y);

    float low = mix(mix(0.0, 2.0, x0),
                    mix(3.0, 1.0, x0),
                    y0);
    float high = mix(mix(0.0, 2.0, x1),
                     mix(3.0, 1.0, x1),
                     y1);
    return low * 4.0 + high;
}

float dither(vec2 pix,float b){
    return b>(bayer4(pix)+0.5)/16.0?1.0:0.0;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec2 coord=floor(fragCoord*0.5)*2.0;
    vec2 uv=(coord-0.5*iResolution.xy)/iResolution.y;
    float r=dot(uv,uv);

    vec2 warped=warp(uv*2.4);
    float wl=max(dot(warped,warped),0.0001);
    warped*=1.0/(0.5+wl*0.8);
    warped*=2.0+1.0/(0.2*iTime+0.5);

    float angle=atan(warped.y,warped.x);
    float dist=inversesqrt(max(dot(warped,warped),0.0001));
    vec3 p3=vec3(cos(angle)*5.5,sin(angle)*5.5,dist+iTime*1.5);

    float fog=smoothNoise3D(p3*0.8)+hash3(p3*2.0-iTime*0.5)*0.2;
    fog*=smoothstep(0.01,0.3,sqrt(r));
    fog=smoothstep(0.025,1.25,fog);

    float n=mix(fbm(warped),fbm(warped*1.7+4.0),0.9);
    vec3 col=palette(n+fog*0.3+0.15*sqrt(r));
    col*=(dither(coord,fog)*0.72+0.28);

    float light=smoothstep(0.2,0.75,fog);
    float g=smoothstep(0.55,1.0,fog);
    float glow=g*g*g;
    float v=1.0-r*0.75;

    vec3 result=(col*light+col*glow*1.2)*v*v;
    result*=result*(3.0-2.0*result);

    // Vinheta extra — aplica por cima de tudo
    result*=1.0-VIGNETTE_EXTRA;

    fragColor=vec4(result,1.0);
}
