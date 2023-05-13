/*
 * @Author: kelise
 * @Date: 2023-05-13 17:28:52
 * @LastEditors: error: git config user.name & please set dead value or install
 * git git git git git git git git git git git git git git git git git
 * @LastEditTime: 2023-05-13 19:46:41
 * @Description: file content
 */
#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

#define MAXINPUT_LENGTH 1024

enum state {
    S_WAIT,          // 初始化状态，未开始处理参数
    S_ARG,           // 正在处理参数
    S_ARG_END,       // 参数处理完毕
    S_ARG_LINE_END,  // 左侧为参数的换行
    S_LINE_END,      // 左侧为空格的换行
    S_END            // 所有参数处理结束
};

enum char_type {
    C_SPACE,     // 空格
    C_LINE_END,  // 换行符
    C_CHAR       // 字符
};

enum char_type get_chartype(char ch) {
    if (ch == ' ') return C_SPACE;
    if (ch == '\n') return C_LINE_END;
    return C_CHAR;
}

void clear_args(char **xargs, int beg) {
    for (int i = beg; i < MAXARG; ++i) xargs[i] = 0;
}

enum state transform_state(enum state cur_st, char *cur_input) {
    char cur_ch = get_chartype(*cur_input);

    switch (cur_st) {
        case S_WAIT:
            if (cur_ch == C_CHAR) return S_ARG;
            if (cur_ch == C_SPACE) return S_WAIT;
            if (cur_ch == C_LINE_END) return C_LINE_END;
        case S_ARG:
            if (cur_ch == C_CHAR) return S_ARG;
            if (cur_ch == C_SPACE) return S_ARG_END;
            if (cur_ch == C_LINE_END) return S_ARG_LINE_END;
        case S_ARG_END:
        case S_LINE_END:
        case S_ARG_LINE_END:
            if (cur_ch == C_CHAR) return S_ARG;
            if (cur_ch == C_SPACE) return S_WAIT;
            if (cur_ch == C_LINE_END) return S_LINE_END;
        default:
            return S_END;
    }
}

int main(int argc, char *argv[]) {
    /* argv[0] = xrags,argv[1...] = 其他参数 */
    if (argc - 1 >= MAXARG) {
        fprintf(2, "too many args\n");
        exit(1);
    }

    char inputs[MAXINPUT_LENGTH];  // xargs的输入
    char *cur_input;               // 指向当前正在处理的输入
    char *xargs[MAXARG];  // xargs自身的参数+存储xargs输入的字符串转换成的参数

    cur_input = inputs;

    for (int i = 1; i < argc; ++i)
        xargs[i - 1] = argv[i];  // 填充xargs自身参数，like：xargs [grep hello]
    int arg_beg = 0;             // 一个参数在输入中的开始位置
    int arg_end = 0;             // 一个参数在输入中的结束位置
    int arg_cnts = argc - 1;  // 当前输入参数索引

    enum state cur_st = S_WAIT;
    while (cur_st != S_END) {
        if (read(0, cur_input, sizeof(char)) != sizeof(char)) {
            cur_st = S_END;
        } else {
            cur_st = transform_state(cur_st, cur_input);
        }

        if (++arg_end >= MAXINPUT_LENGTH) {
            fprintf(2, "arg too long\n");
            exit(1);
        }

        switch (cur_st) {
            case S_WAIT:
                ++arg_beg;
                break;
            case S_ARG_END:
                xargs[arg_cnts++] = &inputs[arg_beg];
                arg_beg = arg_end;
                *cur_input = '\0';
                break;
            case S_ARG_LINE_END:
                xargs[arg_cnts++] = &inputs[arg_beg];
            case S_LINE_END:
                arg_beg = arg_end;
                *cur_input = '\0';
                if (fork() == 0) {
                    exec(argv[1], xargs);
                }
                wait(0);
                arg_cnts = argc - 1;
                clear_args(xargs, arg_cnts);
                break;
            default:
                break;
        }
        ++cur_input;
    }
    exit(0);
}