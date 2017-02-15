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
  if(LOG_CHECK_LEVEL(LS_VERBOSE))
  {
    ostringstream oss;
    LOG_F(LS_VERBOSE) << label << endl <<
      ByteArrayToString(tfb_->fb_, tfb_->fb_ + bytes_transferred_, 16, true);
  }
}

///////////////////////////////////////////////////////////////////////////////

TapFrameCache::TapFrameCache() :
  reservation_count_(0),
  allocation_factor_(2),
  max_allocation_(kCacheIOMax),
  commitment_(0),
  high_threshold_(kCacheIOMax-1),
  master_(kCacheIOMax)
#if !defined(NDEBUG)
  ,realloc_count(0)
#endif
{
  for(auto & e : master_)
  {
    available_.push(&e);
  }
}

TapFrameCache::~TapFrameCache()
{
  //master deletes all the TapFrames regardless of IO state. So ensure that they
  //are all "available_" at this point.
}

uint32_t TapFrameCache::Reservation(size_t entity)
{
  entity;
  return reservation_count_++;
}

uint32_t TapFrameCache::CancelReservation(size_t entity)
{
  entity;
  return --reservation_count_;
}

TapFrame * TapFrameCache::Acquire()
{
  lock_guard<mutex> lg(cache_mtx_);
  TapFrame * tf = nullptr;
  //(commitment_ < reservation_count_ * allocation_factor_) - lazy reduction of the number of active AIOs
  if(commitment_ < high_threshold_ && commitment_ < reservation_count_ * allocation_factor_)
  {
    commitment_++;
    tf = available_.top();
    available_.pop();
  }
  return tf;
}

TapFrame * TapFrameCache::AcquireMustSucceed()
{
  TapFrame * tf = nullptr;
  if(!available_.empty())
  {
    commitment_++;
    tf = available_.top();
    available_.pop();
  }
  else
  {
#if !defined(NDEBUG)
    ++realloc_count;
#endif
    master_.emplace_back();
    tf = &master_.back();
#if !defined(NDEBUG)
    LOG_F(LS_VERBOSE) << "Extending the master TapFrame cache. " <<
      "Reallocation count=" << ++realloc_count;
#endif
  }
  return tf;
}

TapFrame *
TapFrameCache::Acquire(
  const uint8_t * data,
  uint32_t data_len)
{
  if(data_len > kTapBufferSize)
    throw TCEXCEPT("Input data is larger than the maximum allowed");
  TapFrame * tf = nullptr;
  lock_guard<mutex> lg(cache_mtx_);
  if(commitment_ <= high_threshold_)
  {
    commitment_++;
    tf = available_.top();
    available_.pop();
  }
  else
  {
    tf = AcquireMustSucceed();
  }
  if(!tf->tfb_)
    tf->tfb_ = new TapFrameBuffer;
  memcpy(tf->tfb_->fb_, data, data_len);
  tf->BufferToTransfer(tf->tfb_->fb_);
  tf->BytesToTransfer(data_len);
  tf->BytesTransferred(data_len);
  tf->SetWriteOp();
  return tf;
}

void TapFrameCache::Reclaim(TapFrame * tf)
{
  lock_guard<mutex> lg(cache_mtx_);
  available_.push(tf);
  --commitment_;
}

bool TapFrameCache::IsOverProvisioned()
{
  lock_guard<mutex> lg(cache_mtx_);
  if(commitment_ >= high_threshold_)
    return true;
  return false;
}

} //tincan
