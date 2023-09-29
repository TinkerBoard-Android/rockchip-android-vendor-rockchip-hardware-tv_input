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

@VintfStability
interface IRkTvInput {
  void closeRkStream(in int deviceId, in int streamId);
  rockchip.hardware.tv.input.RkTvStreamConfig[] getRkStreamConfigurations(in int deviceId);
  android.hardware.common.NativeHandle[] openRkStream(in int deviceId, in int streamId);
  void privRkCmdFromApp(in rockchip.hardware.tv.input.RkTvPrivAppCmdInfo info);
  void setRkCallback(in android.hardware.tv.input.ITvInputCallback callback);
  void setRkTvMessageEnabled(int deviceId, int streamId, in android.hardware.tv.input.TvMessageEventType type, boolean enabled);
  void getRkTvMessageQueueDesc(out android.hardware.common.fmq.MQDescriptor<byte,android.hardware.common.fmq.SynchronizedReadWrite> queue, int deviceId, int streamId);
  void setRkPreviewInfo(in int deviceId, in int streamId, in int initType);
  void setSinglePreviewBuffer(in long bufId, in android.hardware.common.NativeHandle bufHandle);
  void requestRkCapture(in int deviceId, in int streamId, in long bufId, in int seq);
}
