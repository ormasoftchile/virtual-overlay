/*
 * Copyright 2020-2021 Alpha Cephei Inc.
 *
 * Modified by Virtual Overlay: reduced to the C API declarations used by this
 * project, with pragma once replacing the upstream include guard.
 * Based on https://github.com/alphacep/vosk-api/blob/v0.3.45/src/vosk_api.h
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VoskModel VoskModel;
typedef struct VoskSpkModel VoskSpkModel;
typedef struct VoskRecognizer VoskRecognizer;

void vosk_set_log_level(int log_level);
void vosk_gpu_init();
void vosk_gpu_thread_init();

VoskModel *vosk_model_new(const char *model_path);
void vosk_model_free(VoskModel *model);
int vosk_model_find_word(VoskModel *model, const char *word);

VoskRecognizer *vosk_recognizer_new(VoskModel *model, float sample_rate);
VoskRecognizer *vosk_recognizer_new_spk(VoskModel *model, float sample_rate, VoskSpkModel *spk_model);
VoskRecognizer *vosk_recognizer_new_grm(VoskModel *model, float sample_rate, const char *grammar);
void vosk_recognizer_set_max_alternatives(VoskRecognizer *recognizer, int max_alternatives);
void vosk_recognizer_set_words(VoskRecognizer *recognizer, int words);
void vosk_recognizer_set_partial_words(VoskRecognizer *recognizer, int partial_words);
void vosk_recognizer_set_grm(VoskRecognizer *recognizer, const char *grammar);

int vosk_recognizer_accept_waveform(VoskRecognizer *recognizer, const char *data, int length);
int vosk_recognizer_accept_waveform_s(VoskRecognizer *recognizer, const short *data, int length);
int vosk_recognizer_accept_waveform_f(VoskRecognizer *recognizer, const float *data, int length);

const char *vosk_recognizer_result(VoskRecognizer *recognizer);
const char *vosk_recognizer_partial_result(VoskRecognizer *recognizer);
const char *vosk_recognizer_final_result(VoskRecognizer *recognizer);

void vosk_recognizer_reset(VoskRecognizer *recognizer);
void vosk_recognizer_free(VoskRecognizer *recognizer);

#ifdef __cplusplus
}
#endif
