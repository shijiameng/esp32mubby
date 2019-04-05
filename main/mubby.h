/**
 * MIT License
 *
 * Copyright (c) 2019 Jiameng Shi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _MUBBY_H_
#define _MUBBY_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MUBBY_ID_CORE		(0)

#define MUBBY_ID_PLAYER		(1)

#define PLAYER_STATE_STARTED	(0)
#define PLAYER_STATE_FINISHED	(1)
#define PLAYER_STATE_ABORTED	(2)
#define PLAYER_STATE_ERROR		(3)

#define MUBBY_ID_RECORDER		(2)

#define RECORDER_STATE_STARTED	(0)
#define RECORDER_STATE_FINISHED (1)
#define RECORDER_STATE_ABORTED	(2)
#define RECORDER_STATE_ERROR	(3)

#define MUBBY_ID_WIFIMGR		(3)

#define WIFI_MANAGER_STATE_CONNECTED  	(0)
#define WIFI_MANAGER_STATE_DISCONNECTED (1)

#ifdef __cplusplus
}
#endif

#endif /* _MUBBY_H_ */
