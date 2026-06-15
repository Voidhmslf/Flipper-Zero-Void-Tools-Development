#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>

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
} IntervalometerModel;

// Функция отрисовки (View)
static void draw_callback(Canvas* canvas, void* ctx) {
    IntervalometerModel* m = (IntervalometerModel*)ctx;

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    elements_multiline_text_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Intervalometer");

    canvas_set_font(canvas, FontSecondary);

    const uint8_t x_labels = 10;
    const uint8_t x_values = 85;
    const uint8_t y_start = 22;
    const uint8_t y_step = 13;

    for (int i = 0; i < IntervalometerItemCount; ++i) {
        uint8_t current_y = y_start + (i * y_step);
        bool is_selected = (m->selected_item == i);

        if (is_selected) {
            // Инверсия цветов для выделенного пункта
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_rbox(canvas, 2, current_y - 10, 124, y_step + 1, 2);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_set_color(canvas, ColorBlack);
        }

        // Отрисовка названия параметра и его значения
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

    // Возвращаем черный цвет для нижних кнопок
    canvas_set_color(canvas, ColorBlack);
    elements_button_left(canvas, "-");
    elements_button_right(canvas, "+");
    elements_button_center(canvas, "Start");
}

// Функция обработки ввода
static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    // Кладем событие в очередь
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

// Точка входа в приложение
int32_t template_app(void* p) {
    UNUSED(p);

    // Инициализация очереди событий
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    // Инициализация модели с дефолтными значениями
    IntervalometerModel* model = malloc(sizeof(IntervalometerModel));
    model->delay_s = 2;
    model->interval_s = 5;
    model->shots = 0;
    model->selected_item = IntervalometerItemDelay;

    // Инициализация ViewPort
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, model);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // Подключение ViewPort к GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;

    // Главный цикл приложения
    while(running) {
        // Ожидание события из очереди (таймаут 100 тиков)
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            // Обрабатываем нажатия и удержания кнопок (InputTypePress / InputTypeRepeat)
            if(event.type == InputTypePress || event.type == InputTypeRepeat) {
                switch(event.key) {
                    case InputKeyUp:
                        if(model->selected_item > 0) {
                            model->selected_item--;
                        } else {
                            model->selected_item = IntervalometerItemCount - 1; // Круговая прокрутка вверх
                        }
                        break;
                    case InputKeyDown:
                        if(model->selected_item < IntervalometerItemCount - 1) {
                            model->selected_item++;
                        } else {
                            model->selected_item = 0; // Круговая прокрутка вниз
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
                        // TODO: Start IR timer for Canon 600D
                        break;
                    case InputKeyBack:
                        // Выход из цикла
                        running = false;
                        break;
                    default:
                        break;
                }
                // Запрашиваем перерисовку экрана после изменения состояния
                view_port_update(view_port);
            }
        }
    }

    // Очистка памяти и закрытие записей
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    free(model);

    return 0;
}
