import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('flatvar2.py').read().split('def decode_ok')[0])
def cls(p):
    t, f, bits = p['type'], p['flags'], p.get('bits')
    if t == 0: return 'vi' if f & 0x20 else f'i{bits}'
    if t == 1: return 'f32' if f & 0x4 else f'f{bits}'
    if t == 2: return 'cvec' if f & 0x2 else ('v96' if f & 0x4 else f'v{bits}')
    return f't{t}'
SIG = {
 'CPhysicsProp': {0:'i8',1:'f15',2:'i8',3:'cvec',4:'v24',5:'vi',6:'f32',7:'f32',8:'f32',9:'v96',10:'v96',11:'v96',12:'v96',21:'i1',52:'i1'},
 'CBaseEntity': {4:'i8',5:'cvec',6:'v24',7:'vi',8:'f32',9:'f32',10:'f32',11:'i8',12:'v96',15:'v96',24:'i1',55:'i1'},
 'CDynamicProp': {8:'i8',9:'cvec',10:'v24',11:'vi',12:'f32',14:'f32',15:'v96',18:'v96',27:'i1',58:'i1'},
 'CBaseTrigger': {4:'i8',5:'i8',14:'cvec',15:'v24',16:'vi',17:'f32',19:'f32',20:'i8',21:'v96',24:'v96',33:'i1',64:'i1'},
}
name2dt = {c[1]: c[2] for c in classes}
for mode in ('tf2', 'stable'):
    for drop in ((), ('DT_PredictableId',)):
        print('== mode', mode, 'drop', drop)
        for cn, sig in SIG.items():
            flat = sort(unsorted(name2dt[cn], drop), mode)
            got = {i: cls(flat[i]) for i in sig}
            miss = [f'{i}:{sig[i]}!={got[i]}({flat[i]["name"]})' for i in sig if sig[i] != got[i]]
            print(f'   {cn:14s} mismatches {len(miss)}: {miss[:5]}')
