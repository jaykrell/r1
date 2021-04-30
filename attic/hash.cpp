struct HashEntry;
typedef struct HashEntry HashEntry;

struct HashEntry
{
    uint64_t hash;
    size_t key_size;
    size_t value_size;
    HashEntry* next;
};

struct HashTable
{
    size_t count;
    size_t bucket_count;
    HashEntry** buckets;
    uint64_t (*KeyHash)(void*);
    BOOLEAN (*KeyEq)(void*, void*);
    size_t (*KeySize)(void*);
    size_t (*ValueSize)(void*);
};

void*
HashEntryKey(HashTable* h, HashEntry* entry)
{
    return (char*)(entry) + 1;
}

void*
HashEntryValue(HashTable* h, HashEntry* entry)
{
    return (char*)HashEntryKey(h, entry) + entry->key_size;
}

int
HashLookupInternal(HashTable* h, void* key, uint64_t* hash, size_t* index, HashEntry** entry)
{
    HashEntry* local_entry;
    
    *entry = 0;
    *hash = h->KeyHash(key);
    *index = *hash % h->bucket_count;
    local_entry = h->buckets[index];
    if (!local_entry)
        return 0;
    while (entry)
    {
        if (hash == local_entry->hash && h->KeyEq(key, local_entry + 1))
        {
            *entry = local_entry;
            return 0;
        }
        entry = entry->next;
    }
    return 0;
}

int
HashRemove(HashTable* h, void* key)
{
    HashEntry* local_entry;
    
    *entry = 0;
    *hash = h->KeyHash(key);
    *index = *hash % h->bucket_count;
    local_entry = h->buckets[index];
    if (!local_entry)
        return 0;
    while (entry)
    {
        if (hash == local_entry->hash && h->KeyEq(key, local_entry + 1))
        {
            *entry = local_entry;
            return 0;
        }
        entry = entry->next;
    }
    return 0;
}

int
HashLookup(HashTable* h, void* key, HashEntry** entry)
{
    uint64_t hash;
    size_t index; 
    return HashLookupInternal(h, key, &hash, &index, entry);
}

int
HashNewEntryInternal(HashTable* h, uint64_t hash, size_t index, HashEntry** entry)
{
    if (!(*entry = (HashEntry*)calloc(1, h->key_size + h->value_size)))
        return ENOMEM;
    (*entry)->hash = hash;
    (*entry)->next = h->buckets[index];
    h->buckets[index] = *entry;
    return 0;
}

int
HashAdd(HashTable* h, void* key, void* value, HashEntry** entry)
{
    uint64_t hash;
    size_t index; 
    int err;

    err = HashLookupInternal(h, key, &hash, &index, entry);
    if (*entry || err)
        return err; // already present

    err = HashNewEntryInternal(h, hash, index, entry);
    if (err)
        return err;
    memcpy(HashEntryKey(h, *entry), key, h->key_size);
    memcpy(HashEntryKey(h, *entry), value, h->value_size);
    return 0;
}

int
HashReplace(HashTable* h, void* key, void* value, HashEntry** entry)
{
    uint64_t hash;
    size_t index; 
    int err;

    err = HashLookupInternal(h, key, &hash, &index, entry);
    if (err)
        return err;

    if (!*entry)
    {
        err = HashNewEntryInternal(h, hash, index, entry);
        if (err)
            return err;
    }
    memcpy(HashEntryKey(h, he), key, h->key_size);
    memcpy(HashEntryValue(h, he), value, h->value_size);
    return 0;
}

int
HashAddKeyValue(HashTable* h, void* key, HashEntry** entry)
{
    uint64_t hash;
    size_t index;
    HashEntry* local_entry;
    
    *entry = 0;
    hash = h->KeyHash(key);
    index = hash % h->bucket_count;
    local_entry = h->buckets[index];
    if (!local_entry)
        return 1;
    while (entry)
    {
        if (h->KeyEq(key, local_entry + 1))
        {
            *entry = local_entry;
            return 0;
        }
        entry = entry->next;
    }
}
