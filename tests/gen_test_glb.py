import struct, json, math
def torus(R=1.0, r=0.4, nu=48, nv=24):
    P=[];N=[];UV=[];I=[]
    for i in range(nu+1):
        u=2*math.pi*i/nu
        for j in range(nv+1):
            v=2*math.pi*j/nv
            cx,cz=math.cos(u),math.sin(u)
            x=(R+r*math.cos(v))*cx; y=r*math.sin(v); z=(R+r*math.cos(v))*cz
            P+= [x,y,z]; N+=[math.cos(v)*cx, math.sin(v), math.cos(v)*cz]; UV+=[i/nu*4,j/nv]
    for i in range(nu):
        for j in range(nv):
            a=i*(nv+1)+j; b=a+nv+1
            I+=[a,b,a+1, b,b+1,a+1]
    return P,N,UV,I
def cube():
    P=[];N=[];I=[];UV=[]
    faces=[((0,0,1),(1,0,0),(0,1,0)),((0,0,-1),(-1,0,0),(0,1,0)),((1,0,0),(0,0,-1),(0,1,0)),((-1,0,0),(0,0,1),(0,1,0)),((0,1,0),(1,0,0),(0,0,-1)),((0,-1,0),(1,0,0),(0,0,1))]
    for n,u,v in faces:
        b=len(P)//3
        for su,sv in((-1,-1),(1,-1),(1,1),(-1,1)):
            P+=[n[k]+su*u[k]+sv*v[k] for k in range(3)]; N+=list(n); UV+=[(su+1)/2,(sv+1)/2]
        I+=[b,b+1,b+2,b,b+2,b+3]
    return P,N,UV,I
bin=bytearray(); bvs=[]; accs=[]
def add(data,fmt,ct,typ,count,target=None,mm=False,P=None):
    while len(bin)%4: bin.append(0)
    off=len(bin); raw=struct.pack("<%d%s"%(len(data),fmt),*data); bin.extend(raw)
    bvs.append({"buffer":0,"byteOffset":off,"byteLength":len(raw)})
    a={"bufferView":len(bvs)-1,"componentType":ct,"count":count,"type":typ}
    if mm: a["min"]=[min(P[i::3]) for i in range(3)]; a["max"]=[max(P[i::3]) for i in range(3)]
    accs.append(a); return len(accs)-1
def mesh(P,N,UV,I,mat):
    return {"primitives":[{"attributes":{"POSITION":add(P,"f",5126,"VEC3",len(P)//3,mm=True,P=P),"NORMAL":add(N,"f",5126,"VEC3",len(N)//3),"TEXCOORD_0":add(UV,"f",5126,"VEC2",len(UV)//2)},"indices":add(I,"I",5125,"SCALAR",len(I)),"material":mat}]}
m1=mesh(*torus(),0); m2=mesh(*cube(),1)
# fake image
while len(bin)%4: bin.append(0)
io=len(bin); bin.extend(b"FAKEPNG"); bvs.append({"buffer":0,"byteOffset":io,"byteLength":7})
j={"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],
 "nodes":[{"mesh":0,"rotation":[0.26,0,0,0.966]},{"mesh":1,"translation":[1.8,0.0,-1.2],"scale":[0.4,0.4,0.4],"rotation":[0,0.38,0,0.92]}],
 "meshes":[m1,m2],
 "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0.6,0.3,1],"baseColorTexture":{"index":0}}},{"pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.9,1]}}],
 "textures":[{"source":0}],"images":[{"bufferView":len(bvs)-1,"mimeType":"image/png"}],
 "accessors":accs,"bufferViews":bvs,"buffers":[{"byteLength":len(bin)}]}
js=json.dumps(j).encode()
while len(js)%4: js+=b" "
while len(bin)%4: bin.append(0)
out=struct.pack("<III",0x46546C67,2,12+8+len(js)+8+len(bin))+struct.pack("<II",len(js),0x4E4F534A)+js+struct.pack("<II",len(bin),0x004E4942)+bytes(bin)
import sys; open(sys.argv[1] if len(sys.argv)>1 else "test.glb","wb").write(out)
