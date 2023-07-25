import gdb, traceback;

'''
Usage:
    nn_coroutines
    nn_coroutines 1000
'''
class NnCoroutines(gdb.Command):
    def __init__(self):
        super(NnCoroutines, self).__init__('nn_coroutines', gdb.COMMAND_DATA)

    # https://sourceware.org/gdb/current/onlinedocs/gdb/Python-API.html#Python-API
    def invoke(self, arg, from_tty):
        nn_interval=1000
        nn_coroutines = 1
        # Parse args.
        args = arg.split(' ')
        if args[0] != '':
            nn_interval = int(args[0])
        try:
            pnext = prev = pthis = gdb.parse_and_eval('&_st_this_thread->tlink').__str__()
            print('interval: %s, first: %s, args(%s): %s'%(nn_interval, pthis, len(args), args))
            while True:
                v = gdb.parse_and_eval('((_st_clist_t*)%s)->next'%pnext)
                prev = pnext = str(v.__str__()).split(' ')[0]
                if pnext == pthis:
                    break
                nn_coroutines += 1
                if (nn_coroutines%nn_interval) == 0:
                    print('next is %s, total %s'%(pnext, nn_coroutines))
        except:
            print('Error: prev=%s, this=%s, next=%s, v=%s'%(prev, pthis, pnext, v))
            traceback.print_exc()
        # Result.
        print('total coroutines: %s'%(nn_coroutines))

NnCoroutines()

'''
Usage:
    show_coroutines
'''
class ShowCoroutines(gdb.Command):
    def __init__(self):
        super(ShowCoroutines, self).__init__('show_coroutines', gdb.COMMAND_DATA)

    # https://sourceware.org/gdb/current/onlinedocs/gdb/Python-API.html#Python-API
    def invoke(self, arg, from_tty):
        offset = gdb.parse_and_eval('(int)(&(((_st_thread_t*)(0))->tlink))').__str__()
        _st_this_thread = gdb.parse_and_eval('_st_this_thread').__str__()
        pnext = prev = pthis = gdb.parse_and_eval('&_st_this_thread->tlink').__str__()
        this_thread2 = gdb.parse_and_eval('(_st_thread_t*)(%s - %s)'%(pthis, offset)).__str__()
        #print('offset=%s, _st_this_thread=%s, pthis-offset=%s'%(offset, _st_this_thread, this_thread2))
        try:
            while True:
                trd = gdb.parse_and_eval('(_st_thread_t*)(%s - %s)'%(pnext, offset)).__str__()
                jmpbuf = gdb.parse_and_eval('((_st_thread_t*)%s)->context.__jmpbuf'%(trd)).__str__().split(', ')
                rbp, rsp, crip = int(jmpbuf[1]), int(jmpbuf[6]), None
                if rbp > 0 and rsp > 0:
                    crip = gdb.execute('x/2xa %s'%(rbp), to_string=True).split('\t')[2].strip()
                    print('thread: %s, caller: %s'%(trd, crip))
                v = gdb.parse_and_eval('((_st_clist_t*)%s)->next'%pnext)
                prev = pnext = str(v.__str__()).split(' ')[0]
                if pnext == pthis:
                    break
        except:
            print('Error: prev=%s, this=%s, next=%s'%(prev, pthis, pnext))
            traceback.print_exc()

ShowCoroutines()
