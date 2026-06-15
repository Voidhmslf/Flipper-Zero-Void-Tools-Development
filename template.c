#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>

// --- События и состояния ---

typedef enum {
    EventTypeInput,
    EventTypeTick
} AppEventType;

typedef struct {
    AppEventType type;
    InputEvent input;
} AppEvent;

typedef enum {
    AppStateSetup,
    AppStateRunning
} AppState;

// Перечисление для пунктов меню
typedef enum {
    IntervalometerItemDelay,
    IntervalometerItemInterval,
    IntervalometerItemShots,
    IntervalometerItemCount
} IntervalometerItem;

// Модель состояния
typedef struct {
    uint32_t delay_s;
    uint32_t interval_s;
    uint32_t shots;
    IntervalometerItem selected_item;
    
    AppState state;
    uint32_t remaining_seconds;
    uint32_t current_shot;
} IntervalometerModel;

// --- Отрисовка (View) ---

static void draw_callback(Canvas* canvas, void* ctx) {
    IntervalometerModel* m = (IntervalometerModel*)ctx;

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    elements_multiline_text_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Intervalometer");

    if (m->state == AppStateSetup) {
        // Отрисовка меню настроек
        canvas_set_font(canvas, FontSecondary);
        const uint8_t x_labels = 10;
        const uint8_t x_values = 85;
        const uint8_t y_start = 22;
        const uint8_t y_step = 13;

        for (int i = 0; i < IntervalometerItemCount; ++i) {
            uint8_t current_y = y_start + (i * y_step);
            bool is_selected = (m->selected_item == i);

            if (is_selected) {
                canvas_set_color(canvas, ColorBlack);
                canvas_draw_rbox(canvas, 2, current_y - 10, 124, y_step + 1, 2);
                canvas_set_color(canvas, ColorWhite);
            } else {
                canvas_set_color(canvas, ColorBlack);
            }

            switch (i) {
                case IntervalometerItemDelay: {
                    canvas_draw_str(canvas, x_labels, current_y, "Delay (s):");
                    FuriString* str = furi_string_alloc_printf("%lu", m->delay_s);
                    canvas_draw_str(canvas, x_values, current_y, furi_string_get_cstr(str));
                    furi_string_free(str);
                    break;
                }
                case IntervalometerItemInterval: {
                    canvas_draw_str(canvas, x_labels, current_y, "Interval (s):");
                    FuriString* str = furi_string_alloc_printf("%lu", m->interval_s);
                    canvas_draw_str(canvas, x_values, current_y, furi_string_get_cstr(str));
                    furi_string_free(str);
                    break;
                }
                case IntervalometerItemShots: {
                    canvas_draw_str(canvas, x_labels, current_y, "Shots:");
                    FuriString* str;
                    if (m->shots == 0) {
                        str = furi_string_alloc_printf("Inf");
                    } else {
                        str = furi_string_alloc_printf("%lu", m->shots);
                    }
                    canvas_draw_str(canvas, x_values, current_y, furi_string_get_cstr(str));
                    furi_string_free(str);
                    break;
                }
            }
        }

        canvas_set_color(canvas, ColorBlack);
        elements_button_left(canvas, "-");
        elements_button_right(canvas, "+");
        elements_button_center(canvas, "Start");

    } else if (m->state == AppStateRunning) {
        // Отрисовка экрана прогресса
        canvas_set_font(canvas, FontPrimary);
        
        // Время до следующего кадра
        FuriString* str_time = furi_string_alloc_printf("Next shot in: %lus", m->remaining_seconds);
        elements_multiline_text_aligned(canvas, 64, 25, AlignCenter, AlignCenter, furi_string_get_cstr(str_time));
        furi_string_free(str_time);

        // Номер текущего кадра
        canvas_set_font(canvas, FontSecondary);
        FuriString* str_shot = furi_string_alloc();
        if (m->shots == 0) {
            furi_string_printf(str_shot, "Shot %lu / Inf", m->current_shot);
        } else {
            furi_string_printf(str_shot, "Shot %lu of %lu", m->current_shot, m->shots);
        }
        elements_multiline_text_aligned(canvas, 64, 40, AlignCenter, AlignCenter, furi_string_get_cstr(str_shot));
        furi_string_free(str_shot);

        // Подсказка для кнопки OK
        elements_button_center(canvas, "Stop");
    }
}

// --- Обработка ввода и таймер ---

static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    
    AppEvent event = {
        .type = EventTypeInput,
        .input = *input_event
    };
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

static void timer_callback(void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    
    AppEvent event = {
        .type = EventTypeTick
    };
    // Кладем тик в очередь, если нет места - пропускаем
    furi_message_queue_put(event_queue, &event, 0); 
}

// --- Точка входа ---

int32_t template_app(void* p) {
    UNUSED(p);

    // Очередь событий теперь принимает структуру AppEvent
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(AppEvent));

    // Инициализация модели
    IntervalometerModel* model = malloc(sizeof(IntervalometerModel));
    model->delay_s = 2;
    model->interval_s = 5;
    model->shots = 0;
    model->selected_item = IntervalometerItemDelay;
    
    model->state = AppStateSetup;
    model->remaining_seconds = 0;
    model->current_shot = 0;

    // Инициализация периодического таймера (пока остановлен)
    FuriTimer* timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, event_queue);

    // Настройка ViewPort
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, model);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    AppEvent event;
    bool running = true;

    // Главный цикл обработки событий
    while(running) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            
            // Обработка пользовательского ввода
            if (event.type == EventTypeInput) {
                if(event.input.type == InputTypePress || event.input.type == InputTypeRepeat) {
                    
                    if (model->state == AppStateSetup) {
                        switch(event.input.key) {
                            case InputKeyUp:
                                if(model->selected_item > 0) {
                                    model->selected_item--;
                                } else {
                                    model->selected_item = IntervalometerItemCount - 1;
                                }
                                break;
                            case InputKeyDown:
                                if(model->selected_item < IntervalometerItemCount - 1) {
                                    model->selected_item++;
                                } else {
                                    model->selected_item = 0;
                                }
                                break;
                            case InputKeyLeft:
                                if(model->selected_item == IntervalometerItemDelay && model->delay_s > 0) {
                                    model->delay_s--;
                                } else if(model->selected_item == IntervalometerItemInterval && model->interval_s > 0) {
                                    model->interval_s--;
                                } else if(model->selected_item == IntervalometerItemShots && model->shots > 0) {
                                    model->shots--;
                                }
                                break;
                            case InputKeyRight:
                                if(model->selected_item == IntervalometerItemDelay) {
                                    model->delay_s++;
                                } else if(model->selected_item == IntervalometerItemInterval) {
                                    model->interval_s++;
                                } else if(model->selected_item == IntervalometerItemShots) {
                                    model->shots++;
                                }
                                break;
                            case InputKeyOk:
                                // Переход в состояние запущенного таймера
                                model->state = AppStateRunning;
                                model->current_shot = 0;
                                
                                if (model->delay_s > 0) {
                                    model->remaining_seconds = model->delay_s;
                                } else {
                                    model->remaining_seconds = model->interval_s;
                                }
                                
                                // Запускаем аппаратный таймер раз в секунду
                                furi_timer_start(timer, furi_ms_to_ticks(1000));
                                break;
                            case InputKeyBack:
                                // Выход только из экрана настроек
                                running = false;
                                break;
                            default:
                                break;
                        }
                    } else if (model->state == AppStateRunning) {
                        // Остановка съемки пользователем
                        if (event.input.key == InputKeyBack || event.input.key == InputKeyOk) {
                            furi_timer_stop(timer);
                            model->state = AppStateSetup;
                        }
                    }
                    view_port_update(view_port);
                }
            } 
            // Обработка тиков таймера (1 секунда)
            else if (event.type == EventTypeTick) {
                if (model->state == AppStateRunning) {
                    if (model->remaining_seconds > 0) {
                        model->remaining_seconds--;
                    }
                    
                    // Когда время вышло, делаем снимок
                    if (model->remaining_seconds == 0) {
                        // TODO: IR Launch
                        model->current_shot++;
                        
                        // Сброс таймера на интервал между кадрами
                        model->remaining_seconds = model->interval_s;
                        
                        // Если достигли нужного количества кадров — останавливаем
                        if (model->shots > 0 && model->current_shot >= model->shots) {
                            furi_timer_stop(timer);
                            model->state = AppStateSetup;
                        }
                    }
                    view_port_update(view_port);
                }
            }
        }
    }

    // Освобождение ресурсов
    furi_timer_free(timer);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    free(model);

    return 0;
}
