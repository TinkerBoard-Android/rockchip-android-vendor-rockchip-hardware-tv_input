/*
 * Copyright 2023 Rockchip Electronics S.LSI Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package rockchip.hardware.tv.input;

import android.hardware.tv.input.TvStreamConfig;

@VintfStability
parcelable RkTvStreamConfig {
    TvStreamConfig base;

    int deviceId;
    int width;
    int height;
    int format;
    long usage;
    int buffCount;
}
