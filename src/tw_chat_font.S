#if defined(TDECK_PLUS) || defined(SQW_CHAT_TEXT_TEST)
#ifndef SQW_CHAT_FONT_PATH
#define SQW_CHAT_FONT_PATH "assets/chat-font/tw-chat-glyphs.bin"
#endif
#ifdef __APPLE__
#define CHAT_FONT_BEGIN _sqw_tw_chat_font_start
#define CHAT_FONT_END _sqw_tw_chat_font_end
    .section __TEXT,__const
#else
#define CHAT_FONT_BEGIN sqw_tw_chat_font_start
#define CHAT_FONT_END sqw_tw_chat_font_end
    .section .rodata.tw_chat_font,"a",@progbits
#endif
    .balign 2
    .global CHAT_FONT_BEGIN
    .global CHAT_FONT_END
CHAT_FONT_BEGIN:
    .incbin SQW_CHAT_FONT_PATH
CHAT_FONT_END:
#endif

#if defined(SQW_CHAT_TEXT_TEST) && defined(__ELF__)
    .section .note.GNU-stack,"",@progbits
#endif
