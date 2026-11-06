from tools import *
import subprocess,sys,glob,os,random
NEW=os.path.abspath('../driver/build/rastertosp211')
def run_new(ras,out,title='title',user='user',copies='1',opts=''):
    with open(out,'wb') as f: r=subprocess.run([NEW,'1',user,title,copies,opts,ras],stdout=f,stderr=subprocess.PIPE)
    return r.returncode
def compare(name,ras,**kw):
    run_orig(ras,'var/o.out',**kw); rc=run_new(ras,'var/n.out',**kw)
    a=open('var/o.out','rb').read(); b=open('var/n.out','rb').read()
    if a==b: print('OK  ',name,len(a)); return True
    k=next((i for i in range(min(len(a),len(b))) if a[i]!=b[i]),min(len(a),len(b)))
    print('FAIL',name,'rc',rc,'len',len(a),len(b),'first diff @',hex(k),a[k-8:k+8].hex(),b[k-8:k+8].hex()); return False
