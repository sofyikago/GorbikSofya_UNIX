#include <pthread.h>
#include <unistd.h>
#include <iostream>

class Event
{
public:
    explicit Event(int id) : id(id) {}
    int id;
};

class Monitor
{
public:
    bool provide(Event *event)
    {
        pthread_mutex_lock(&mutex);
        if (ready)
        {
            pthread_mutex_unlock(&mutex);
            return false;
        }
        data = event;
        ready = true;
        std::cout << "Отправлено событие " << event->id << std::endl;
        pthread_cond_signal(&condition);
        pthread_mutex_unlock(&mutex);
        return true;
    }

    void consume()
    {
        pthread_mutex_lock(&mutex);
        while (!ready)
            pthread_cond_wait(&condition, &mutex);
        Event *event = static_cast<Event *>(data);
        data = nullptr;
        ready = false;
        std::cout << "Обработано событие " << event->id << std::endl;
        delete event;
        pthread_mutex_unlock(&mutex);
    }

private:
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
    bool ready = false;
    void *data = nullptr;
};

Monitor monitor;

void *producer(void *)
{
    for (int id = 1;; ++id)
    {
        sleep(1);
        Event *event = new Event(id);
        while (!monitor.provide(event))
            sleep(1);
    }
    return nullptr;
}

void *consumer(void *)
{
    for (;;)
        monitor.consume();
    return nullptr;
}

int main()
{
    pthread_t producerThread, consumerThread;
    pthread_create(&consumerThread, nullptr, consumer, nullptr);
    pthread_create(&producerThread, nullptr, producer, nullptr);
    pthread_join(producerThread, nullptr);
    return 0;
}
