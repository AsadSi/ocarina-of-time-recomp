#include <fstream>
#include <sstream>
#include <unordered_map>
#include <chrono>
#include <array>

#include "librecomp/helpers.hpp"
#include "recomp_input.h"
#include "ultramodern/ultramodern.hpp"

// Arrays that hold the mappings for every input for keyboard and controller respectively.
using input_mapping = std::array<recomp::InputField, recomp::bindings_per_input>;
using input_mapping_array = std::array<input_mapping, static_cast<size_t>(recomp::GameInput::COUNT)>;
static input_mapping_array keyboard_input_mappings{};
static input_mapping_array controller_input_mappings{};

// Make the button value array, which maps a button index to its bit field.
#define DEFINE_INPUT(name, value, readable) uint16_t(value##u),
static const std::array n64_button_values = {
    DEFINE_N64_BUTTON_INPUTS()
};
#undef DEFINE_INPUT

// Make the input name array.
#define DEFINE_INPUT(name, value, readable) readable,
static const std::vector<std::string> input_names = {
    DEFINE_ALL_INPUTS()
};
#undef DEFINE_INPUT

// Make the input enum name array.
#define DEFINE_INPUT(name, value, readable) #name,
static const std::vector<std::string> input_enum_names = {
    DEFINE_ALL_INPUTS()
};
#undef DEFINE_INPUT

size_t recomp::get_num_inputs() {
    return (size_t)GameInput::COUNT;
}

const std::string& recomp::get_input_name(GameInput input) {
    return input_names.at(static_cast<size_t>(input));
}

const std::string& recomp::get_input_enum_name(GameInput input) {
    return input_enum_names.at(static_cast<size_t>(input));
}

recomp::GameInput recomp::get_input_from_enum_name(const std::string_view enum_name) {
    auto find_it = std::find(input_enum_names.begin(), input_enum_names.end(), enum_name);
    if (find_it == input_enum_names.end()) {
        return recomp::GameInput::COUNT;
    }

    return static_cast<recomp::GameInput>(find_it - input_enum_names.begin());
}

// Due to an RmlUi limitation this can't be const. Ideally it would return a const reference or even just a straight up copy.
recomp::InputField& recomp::get_input_binding(GameInput input, size_t binding_index, recomp::InputDevice device) {
    input_mapping_array& device_mappings = (device == recomp::InputDevice::Controller) ?  controller_input_mappings : keyboard_input_mappings;
    input_mapping& cur_input_mapping = device_mappings.at(static_cast<size_t>(input));

    if (binding_index < cur_input_mapping.size()) {
        return cur_input_mapping[binding_index];
    }
    else {
        static recomp::InputField dummy_field = {};
        return dummy_field;
    }
}

void recomp::set_input_binding(recomp::GameInput input, size_t binding_index, recomp::InputDevice device, recomp::InputField value) {
    input_mapping_array& device_mappings = (device == recomp::InputDevice::Controller) ?  controller_input_mappings : keyboard_input_mappings;
    input_mapping& cur_input_mapping = device_mappings.at(static_cast<size_t>(input));

    if (binding_index < cur_input_mapping.size()) {
        cur_input_mapping[binding_index] = value;
    }
}

// Development aid: when RECOMP_INPUT_SCRIPT names a file, controller 1 plays it instead of live input.
// Each line is "<seconds> [button...] [X=<-1..1>] [Y=<-1..1>]": from that many seconds after the first
// poll, hold exactly those buttons and that stick position until the next line. '#' starts a comment.
namespace {
    struct ScriptStep {
        double time;
        uint16_t buttons;
        float x, y;
    };

    std::vector<ScriptStep> load_input_script(const char* path) {
        static const std::unordered_map<std::string, uint16_t> names = {
            {"A", 0x8000}, {"B", 0x4000}, {"Z", 0x2000}, {"START", 0x1000},
            {"DU", 0x0800}, {"DD", 0x0400}, {"DL", 0x0200}, {"DR", 0x0100},
            {"L", 0x0020}, {"R", 0x0010}, {"CU", 0x0008}, {"CD", 0x0004}, {"CL", 0x0002}, {"CR", 0x0001},
        };
        std::vector<ScriptStep> steps;
        std::ifstream file{ path };
        std::string line;
        while (std::getline(file, line)) {
            line = line.substr(0, line.find('#'));
            std::istringstream words{ line };
            ScriptStep step{};
            if (!(words >> step.time)) {
                continue;
            }
            std::string word;
            while (words >> word) {
                if (word.starts_with("X=")) {
                    step.x = std::stof(word.substr(2));
                }
                else if (word.starts_with("Y=")) {
                    step.y = std::stof(word.substr(2));
                }
                else if (auto it = names.find(word); it != names.end()) {
                    step.buttons |= it->second;
                }
                else {
                    fprintf(stderr, "Input script: unknown button \"%s\"\n", word.c_str());
                }
            }
            steps.push_back(step);
        }
        return steps;
    }

    bool get_scripted_input(uint16_t* buttons_out, float* x_out, float* y_out) {
        static const char* path = getenv("RECOMP_INPUT_SCRIPT");
        if (path == nullptr) {
            return false;
        }
        static const std::vector<ScriptStep> steps = load_input_script(path);
        static const auto start = std::chrono::steady_clock::now();
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        ScriptStep current{};
        for (const ScriptStep& step : steps) {
            if (step.time > now) {
                break;
            }
            current = step;
        }
        *buttons_out = current.buttons;
        *x_out = current.x;
        *y_out = current.y;
        return true;
    }
}

bool recomp::get_n64_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out) {
    if (controller_num == 0 && get_scripted_input(buttons_out, x_out, y_out)) {
        return true;
    }

    uint16_t cur_buttons = 0;
    float cur_x = 0.0f;
    float cur_y = 0.0f;
    
    if (controller_num != 0) {
        return false;
    }

    if (!recomp::game_input_disabled()) {
        for (size_t i = 0; i < n64_button_values.size(); i++) {
            size_t input_index = (size_t)GameInput::N64_BUTTON_START + i;
            cur_buttons |= recomp::get_input_digital(keyboard_input_mappings[input_index]) ? n64_button_values[i] : 0;
            cur_buttons |= recomp::get_input_digital(controller_input_mappings[input_index]) ? n64_button_values[i] : 0;
        }

        float joystick_deadzone = recomp::get_joystick_deadzone() / 100.0f;

        float joystick_x = recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::X_AXIS_POS])
                        - recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::X_AXIS_NEG]);

        float joystick_y = recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::Y_AXIS_POS])
                        - recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::Y_AXIS_NEG]);

        recomp::apply_joystick_deadzone(joystick_x, joystick_y, &joystick_x, &joystick_y);

        cur_x = recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::X_AXIS_POS])
                - recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::X_AXIS_NEG]) + joystick_x;

        cur_y = recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::Y_AXIS_POS])
                - recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::Y_AXIS_NEG]) + joystick_y;
    }

    *buttons_out = cur_buttons;
    *x_out = std::clamp(cur_x, -1.0f, 1.0f);
    *y_out = std::clamp(cur_y, -1.0f, 1.0f);

    return true;
}
