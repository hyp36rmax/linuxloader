/* The standalone persistence tests omit the unrelated production log frontend. */
int logGeneric(int level, const char *file, int line, const char *message, ...)
{
    (void)level;
    (void)file;
    (void)line;
    (void)message;
    return 0;
}
