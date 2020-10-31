import gdb, sys, traceback;

class NnCouroutines(gdb.Command):
    def __init__(self):
        super(NnCouroutines, self).__init__('nn_coroutines', gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        try:
            if len(arg) > 0:
                prev = pthis = arg
                print('start from %s'%(arg))
            else:
                prev = pthis = gdb.parse_and_eval('&_st_this_thread->tlink').__str__()
                print('start from &_st_this_thread->tlink')
            print('first is: %s'%(pthis))
            nn_coroutines, pnext = 1, pthis
            while True:
                v = gdb.parse_and_eval('((_st_clist_t*)%s)->next'%pnext)
                prev = pnext = str(v.__str__()).split(' ')[0]
                if pnext == pthis:
                    break
                nn_coroutines += 1
                if len(arg) > 0 or (nn_coroutines%1000) == 0:
                    print('next is %s, total %s'%(pnext, nn_coroutines))
            print('total coroutines: %s'%(nn_coroutines))
        except:
            print('Error: prev=%s, this=%s, next=%s, v=%s'%(prev, pthis, pnext, v))
            traceback.print_exc()

NnCouroutines()