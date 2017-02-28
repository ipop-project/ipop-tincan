/*
* ipop-project
* Copyright 2016, University of Florida
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/
#include "tap_frame.h"
#include "tincan_exception.h"
namespace tincan
{
TapFrame::TapFrame(bool alloc_tfb) :
  AsyncIo(),
  tfb_(nullptr)
{
  if(alloc_tfb)
  {
    tfb_ = new TapFrameBuffer;
    AsyncIo::Initialize(tfb_->fb_, kTapBufferSize, this, AIO_READ, 0);
  }
  else
    AsyncIo::Initialize(tfb_->fb_, 0, this, AIO_READ, 0);
}

TapFrame::TapFrame(const TapFrame & rhs) :
  AsyncIo(),
  tfb_(nullptr)
{
  if(rhs.tfb_)
  {
    tfb_ = new TapFrameBuffer;
    memcpy(tfb_->fb_, rhs.tfb_->fb_, sizeof(rhs.tfb_->fb_));
    AsyncIo::Initialize(tfb_->fb_, rhs.bytes_to_transfer_, this,
      rhs.flags_, rhs.bytes_transferred_);
  }
}

TapFrame::TapFrame(TapFrame && rhs) :
  AsyncIo(),
  tfb_(rhs.tfb_)
{
  rhs.tfb_ = nullptr;
  AsyncIo::Initialize(tfb_->fb_, rhs.bytes_to_transfer_, this,
    rhs.flags_, rhs.bytes_transferred_);
}

/*
Copies the specifed amount of payload data into the TFB. The input buffer is
expected to have kTapHeader UID headers. It also sets up the frame for write
IO.
*/
TapFrame::TapFrame(
  uint8_t * in_buf,
  uint32_t buf_len)
{
  if(buf_len > kTapBufferSize)
    throw TCEXCEPT("Input data is larger than the maximum allowed");
  tfb_ = new TapFrameBuffer;
  memcpy(tfb_->fb_, in_buf, buf_len);
  AsyncIo::Initialize(tfb_->fb_, buf_len, this, AIO_WRITE, buf_len);
}

TapFrame::~TapFrame()
{
  delete tfb_;
}

TapFrame &
TapFrame::operator= (TapFrame & rhs)
{
  if(!tfb_)
    tfb_ = new TapFrameBuffer;
  memcpy(tfb_->fb_,rhs.tfb_->fb_, rhs.Capacity());

  AsyncIo::Initialize(tfb_->fb_, rhs.bytes_to_transfer_, this,
    rhs.flags_, rhs.bytes_transferred_);
  return *this;
}

TapFrame &
TapFrame::operator= (TapFrame && rhs)
{
  if(this->tfb_) delete this->tfb_;
  this->tfb_ = rhs.tfb_;

  AsyncIo::Initialize(tfb_->fb_, rhs.bytes_to_transfer_, this,
    rhs.flags_, rhs.bytes_transferred_);

  rhs.tfb_ = nullptr;
  rhs.buffer_to_transfer_ = nullptr;
  rhs.context_ = nullptr;
  rhs.bytes_transferred_ = 0;
  rhs.bytes_to_transfer_ = 0;
  rhs.flags_ = AIO_WRITE;
  return *this;
}

bool
TapFrame::operator==(
  const TapFrame & rhs) const
{
  return (tfb_ == rhs.tfb_);
}

bool
TapFrame::operator!=(
  const TapFrame & rhs) const
{
  return !(*this == rhs);
}

bool
TapFrame::operator !() const
{
  return tfb_ == nullptr;
}

uint8_t &
TapFrame::operator [](
  uint32_t index)
{
  if(!tfb_ || index >= sizeof(tfb_->fb_))
    throw TCEXCEPT("TapFrameBuffer index out of bounds");
  return reinterpret_cast<uint8_t*>(tfb_->fb_)[index];
}

const uint8_t &
TapFrame::operator [](
  const uint32_t index) const
{
  if(!tfb_ || index >= sizeof(tfb_->fb_))
    throw TCEXCEPT("TapFrameBuffer index out of bounds");
  return reinterpret_cast<uint8_t*>(tfb_->fb_)[index];
}

TapFrame & TapFrame::Initialize()
{
  if(!tfb_)
  {
    tfb_ = new TapFrameBuffer;
  }
  AsyncIo::Initialize(tfb_->fb_, kTapBufferSize, this, AIO_READ, 0);
  return *this;
}

const uint8_t * TapFrame::EthernetHeader() const
{
  if(!tfb_)
    return nullptr;
  return (const uint8_t*)tfb_->fb_;
}
uint8_t * TapFrame::EthernetHeader()
{
  if(!tfb_)
    return nullptr;
  return tfb_->fb_;
}

uint8_t * TapFrame::begin()
{
  if(!tfb_)
    return end();
  return tfb_->fb_;
}

uint8_t * TapFrame::end()
{
  if(!tfb_)
    return nullptr;
  return tfb_->fb_+kTapBufferSize; //one after last valid byte
}

uint8_t * TapFrame::EthernetPl()
{
  if(!tfb_)
    return nullptr;
  return &tfb_->fb_[14];
}

uint32_t TapFrame::Capacity() const
{
  if(!tfb_)
    return 0;
  return sizeof(tfb_->fb_);
}

void TapFrame::Dump(const string & label)
{
  if(LOG_CHECK_LEVEL(LS_ERROR)) //!LS_VERBOSE
  {
    ostringstream oss;
    LOG(LS_ERROR) << label << endl << //!LS_VERBOSE
      ByteArrayToString(tfb_->fb_, tfb_->fb_ + bytes_transferred_, 16, true);
  }
}
} //tincan
