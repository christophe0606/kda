"""Audit only owned batch call sites, never the comparator body."""
import re


def audit(disassembly):
    bodies = {}
    targets = {'fir_batch_candidate':'kda_fir_f32', 'fir_batch_baseline':'arm_fir_f32',
               'fir_batch_empty':'empty_call', 'fir_batch_control':None}
    for name, target in targets.items():
        part = disassembly.split('<'+name+'>:',1)[1].split('\n\n',1)[0]
        instructions = []
        for line in part.splitlines():
            m = re.match(r'\s*[0-9a-f]+:\s+(?:[0-9a-f]{4}\s+)+\s*(\S+)\s*(.*)',line)
            if m:
                op, args = m.groups()
                instructions.append((op,args.split(' @')[0].strip()))
                if op.startswith('pop'):
                    break
        calls = [args for op,args in instructions if op=='bl']
        if len(calls) != (128 if target else 0) or any('<'+str(target)+'>' not in c for c in calls):
            raise ValueError('Unexpected public call shape: '+name)
        normalized = []
        for op,args in instructions:
            if op in ('bl','beq.w','bne.w'):
                args = 'public' if op=='bl' else 'loop'
            normalized.append((op,args))
        bodies[name] = normalized
    if bodies['fir_batch_candidate'] != bodies['fir_batch_baseline']:
        raise ValueError('Mismatched candidate/reference call sites')
    if bodies['fir_batch_candidate'] != bodies['fir_batch_empty']:
        raise ValueError('Mismatched ABI-empty call sites')
    full = bodies['fir_batch_candidate']
    if full[:7]+full[-3:] != bodies['fir_batch_control']:
        raise ValueError('Control scaffolding differs from timed call loops')
    call = [('mov','r0, r5'),('mov','r1, r6'),('mov','r2, r7'),('mov','r3, r8'),('bl','public')]
    if full[7:-3] != call*128:
        raise ValueError('Unexpected work between public calls')
    return {'calls_per_iteration':128,'matched_call_sites':True,'matched_loop_control':True}
