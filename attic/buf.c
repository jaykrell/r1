size_t Buf_capacity(Buf* buf);
int Buf_reserve(Buf* buf, size_t size);
void Buf_clear(Buf* buf);
size_t Buf_size(Buf* buf);
void* Buf_at(Buf* buf, size_t index);
int Buf_push_back(Buf* buf, const void* element);

int Buf_push_back(Buf* buf, const void* element)
{
    size_t size = buf->size;
    int err = Buf_reserve(buf, 1 + size);
    if (err)
        return err;
    return buf->element_type->copy(Buf_at(buf, size), element);
}

size_t Buf_size(Buf* buf)
{
    return buf->size;
}

void Buf_pop_back(Buf* buf)
{
    size_t size = buf->size;
    if (size)
    {
        buf->element_type->cleanup(Buf_at(buf, size), 1);
        buf->size = size - 1;
    }
}

void Buf_clear(Buf* buf)
{
    size_t size = buf->size;
    if (size)
    {
        buf->element_type->cleanup(buf->data, size);
        buf->size = 0;
    }
}

int Buf_reserve(Buf* buf, size_t size)
{
    void* next;
    size_t element_size;

    if (size <= buf->capacity)
        return;
    element_size = buf->element_type->size;
    next = calloc(size * element_size);
    if (!next)
        return -1;
    if (buf->data)
        memcpy(next, buf->data, buf->size * element_size);
    buf->capacity = size;
}

size_t Buf_capacity(Buf* buf)
{
    return buf->capacity;
}

void* Buf_at(Buf* buf, size_t index)
{
    return ((char*)buf->data) + index * buf->element_type->size;
}

