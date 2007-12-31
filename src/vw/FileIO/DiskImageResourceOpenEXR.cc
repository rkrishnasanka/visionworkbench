// __BEGIN_LICENSE__
// 
// Copyright (C) 2006 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration
// (NASA).  All Rights Reserved.
// 
// Copyright 2006 Carnegie Mellon University. All rights reserved.
// 
// This software is distributed under the NASA Open Source Agreement
// (NOSA), version 1.3.  The NOSA has been approved by the Open Source
// Initiative.  See the file COPYING at the top of the distribution
// directory tree for the complete NOSA document.
// 
// THE SUBJECT SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF ANY
// KIND, EITHER EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT NOT
// LIMITED TO, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL CONFORM TO
// SPECIFICATIONS, ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR
// A PARTICULAR PURPOSE, OR FREEDOM FROM INFRINGEMENT, ANY WARRANTY THAT
// THE SUBJECT SOFTWARE WILL BE ERROR FREE, OR ANY WARRANTY THAT
// DOCUMENTATION, IF PROVIDED, WILL CONFORM TO THE SUBJECT SOFTWARE.
// 
// __END_LICENSE__

/// \file FileIO/DiskImageResourceOpenEXR.cc
/// 
/// Provides support for the OpenEXR file format.
///

#ifdef _MSC_VER
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4996)
#endif

#include <vector>

#include <ImfInputFile.h>
#include <ImfTiledInputFile.h>
#include <ImfOutputFile.h>
#include <ImfTiledOutputFile.h>
#include <ImfChannelList.h>
#include <ImfStringAttribute.h>
#include <ImfMatrixAttribute.h>
#include <ImfArray.h>
#include <ImfLineOrder.h>
#include <ImfChannelList.h>

#include <vw/Core/Exception.h>
#include <vw/Image/PixelTypes.h>
#include <vw/Image/Statistics.h>
#include <vw/FileIO/DiskImageResourceOpenEXR.h>

namespace {

  // This little type computation routine helps us to determine what
  // to label the channels in the OpenEXR file given the pixel type of
  // the source image.
  static std::string openexr_channel_string_of_pixel_type(const int pixel_format, 
                                                          const int channel) {
    if (pixel_format == vw::VW_PIXEL_RGB) {
      switch (channel) {
      case 0 : return "R"; break;
      case 1 : return "G"; break;
      case 2 : return "B"; break;
      default: vw_throw( vw::ArgumentErr() << "ChannelStringOfPixelType: Invalid channel number (" << channel << ")" );
      }

    } else if (pixel_format == vw::VW_PIXEL_RGBA) {
      switch (channel) {
      case 0 : return "R"; break;
      case 1 : return "G"; break;
      case 2 : return "B"; break;
      case 3 : return "A"; break;
      default: vw_throw( vw::ArgumentErr() << "ChannelStringOfPixelType: Invalid channel number (" << channel << ")" );
      }
    
    }
    // Default case:
    std::ostringstream m_stream;
    m_stream << "Channel" << channel; 
    return m_stream.str();

  }

}


// The destructor is here, despite being so brief, because deleting 
// an object safely requires knowing its full type.
vw::DiskImageResourceOpenEXR::~DiskImageResourceOpenEXR() {
  if (m_input_file_ptr) 
    if (m_tiled)
      delete static_cast<Imf::TiledInputFile*>(m_input_file_ptr);
    else 
      delete static_cast<Imf::InputFile*>(m_input_file_ptr);
  if (m_output_file_ptr) {
    if (m_tiled)
      delete static_cast<Imf::TiledOutputFile*>(m_output_file_ptr);
    else 
      delete static_cast<Imf::OutputFile*>(m_output_file_ptr);
  }
}

vw::Vector2i vw::DiskImageResourceOpenEXR::native_block_size() const {
  return m_block_size;
}

// Bind the resource to a file for reading.  Confirm that we can open
// the file and that it has a sane pixel format.  In general VIL does 
// not give us any useful information about the pixel format, so we 
// make some guesses here based on the channel count.
void vw::DiskImageResourceOpenEXR::open( std::string const& filename )
{
  try {
    // Open Image file and read the header
    m_filename = filename;
    
    // Check to make sure that the file_ptr is not already in use.
    if (m_input_file_ptr) 
      vw_throw( IOErr() << "Disk image resources do not yet support reuse." );
    
    m_input_file_ptr = new Imf::InputFile(filename.c_str());

    // Check to see if the file is tiled.  If it does, close the descriptor and reopen as a tiled file.
    if (static_cast<Imf::InputFile*>(m_input_file_ptr)->header().hasTileDescription()) {
      delete static_cast<Imf::InputFile*>(m_input_file_ptr);
      m_input_file_ptr = new Imf::TiledInputFile(filename.c_str());
      m_tiled = true;
    }
      
    // Find the width and height of the image 
    Imath::Box2i dw = static_cast<Imf::InputFile*>(m_input_file_ptr)->header().dataWindow();
    m_format.cols  = int(dw.max.x - dw.min.x + 1);
    m_format.rows  = int(dw.max.y - dw.min.y + 1);
    
    // Determine the number of image channels 
    Imf::ChannelList::ConstIterator iter = static_cast<Imf::InputFile*>(m_input_file_ptr)->header().channels().begin();
    int num_channels = 0;
    while( iter++ != static_cast<Imf::InputFile*>(m_input_file_ptr)->header().channels().end() )
      num_channels++;
    m_format.planes = num_channels;
    
    // For now, we only support reading in multi-plane, single channel
    // images.
    m_format.pixel_format = VW_PIXEL_SCALAR;

    if (m_tiled) {
      Imf::TileDescription desc = static_cast<Imf::InputFile*>(m_input_file_ptr)->header().tileDescription();
      m_block_size = Vector2i(desc.xSize, desc.ySize);
    } else {
      m_block_size = Vector2i(m_format.cols,m_openexr_rows_per_block);
    }

  } catch (Iex::BaseExc e) {
    vw_throw( vw::IOErr() << "DiskImageResourceOpenEXR: could not open " << filename << ":\n\t" << e.what() ); 
  } 
}


void vw::DiskImageResourceOpenEXR::set_tiled_write(int32 tile_width, int32 tile_height, bool random_tile_order) {
  m_tiled = true;
  m_block_size = Vector2i(tile_width, tile_height);
  
  // Close and reopen the file
  if (m_output_file_ptr) {
    if (m_tiled)
      delete static_cast<Imf::TiledOutputFile*>(m_output_file_ptr);
    else 
      delete static_cast<Imf::OutputFile*>(m_output_file_ptr);
  }

  try {      
    // Create the file header with the appropriate number of
    // channels.  Label the channels in order starting with "Channel 0".
    Imf::Header header (m_format.cols,m_format.rows);
    for ( int32 nn = 0; nn < m_format.planes; nn++) {
      m_labels[nn] = openexr_channel_string_of_pixel_type(m_format.pixel_format, nn);
      header.channels().insert (m_labels[nn].c_str(), Imf::Channel (Imf::FLOAT));
    }

    header.setTileDescription(Imf::TileDescription (m_block_size[0], m_block_size[1], Imf::ONE_LEVEL)); 

    // Instruct the OpenEXR library to write tiles to the file in
    // whatever order they are given.  Otherwise, OpenEXR will buffer
    // out-of-order tiles until it is their turn to be written to
    // disk.
    if (random_tile_order)
      header.lineOrder() = Imf::RANDOM_Y;

    m_output_file_ptr = new Imf::TiledOutputFile(m_filename.c_str(), header);
  } catch (Iex::BaseExc e) {
    vw_throw( vw::IOErr() << "DiskImageResourceOpenEXR: Failed to create " << m_filename << ".\n\t" << e.what() );
  }

}

void vw::DiskImageResourceOpenEXR::set_scanline_write(int32 scanlines_per_block) {
  m_tiled = false;
  m_block_size = Vector2i(m_format.cols,scanlines_per_block);
  
  // Close and reopen the file
  if (m_output_file_ptr) {
    if (m_tiled)
      delete static_cast<Imf::TiledOutputFile*>(m_output_file_ptr);
    else 
      delete static_cast<Imf::OutputFile*>(m_output_file_ptr);
  }

  try {      
    // Create the file header with the appropriate number of
    // channels.  Label the channels in order starting with "Channel 0".
    Imf::Header header (m_format.cols,m_format.rows);
    for ( int32 nn = 0; nn < m_format.planes; nn++) {
      m_labels[nn] = openexr_channel_string_of_pixel_type(m_format.pixel_format, nn);
      header.channels().insert (m_labels[nn].c_str(), Imf::Channel (Imf::FLOAT));
    }
    header.lineOrder() = Imf::INCREASING_Y;

    m_block_size = Vector2i(m_format.cols,m_openexr_rows_per_block);
    m_output_file_ptr = new Imf::OutputFile(m_filename.c_str(), header);

  } catch (Iex::BaseExc e) {
    vw_throw( vw::IOErr() << "DiskImageResourceOpenEXR: Failed to create " << m_filename << ".\n\t" << e.what() );
  }
}

// Bind the resource to a file for writing.  
void vw::DiskImageResourceOpenEXR::create( std::string const& filename, 
                                           ImageFormat const& format )
{
  VW_ASSERT(format.planes == 1 || format.pixel_format==VW_PIXEL_SCALAR,
            NoImplErr() << "DiskImageResourceOpenEXR: Cannot create " << filename << "\n\t"
            << "The image cannot have both multiple channels and multiple planes.\n");
  
  m_filename = filename;
  m_format = format;
  m_format.channel_type = VW_CHANNEL_FLOAT32;
  m_format.planes = std::max( format.planes, num_channels( format.pixel_format ) );

  // Open the EXR file and set up the header information
  m_labels.resize(m_format.planes);
  
  // By default, write out the image is a tiled image.
  this->set_tiled_write(2048,2048);
}

// Read the disk image into the given buffer.
void vw::DiskImageResourceOpenEXR::read( ImageBuffer const& dest, BBox2i const& bbox ) const
{
  vw_out(VerboseDebugMessage, "fileio") << "DiskImageResourceOpenEXR: Reading OpenEXR Block " << bbox << "\n";
  
  if (!m_input_file_ptr) 
    vw_throw( LogicErr() << "DiskImageResourceOpenEXR: Could not read file. No file has been opened." );
  
  try {
    Imf::Header header;
    if (m_tiled) 
      header = static_cast<Imf::TiledInputFile*>(m_input_file_ptr)->header();
    else 
      header = static_cast<Imf::InputFile*>(m_input_file_ptr)->header();

    // Find the width and height of the image and set the data window
    // to the beginning of the requesed block.
    int32 height = bbox.height();
    int32 width = bbox.width();

    // Set up the OpenEXR data structures necessary to read all of
    // the channels out of the file and execute the call to readPixels().
    Imf::ChannelList::ConstIterator channel = header.channels().begin();
    std::vector<std::string> channel_names(m_format.planes);
    for (int i=0; channel != header.channels().end(); ++channel, ++i) {
      channel_names[i] = channel.name();
    }
    
    // OpenEXR seems to order channels in the file alphabetically,
    // rather than in the order in which they were saved.  This means
    // that we need to reorder the channel names when they are
    // labelled as RGB or RGBA.  For other channel naming schemes, we
    // just go with alphabetical, since that's all we've got.
    if ( m_format.planes == 3 ) {
      if (find(channel_names.begin(), channel_names.end(), "R") != channel_names.end() &&
          find(channel_names.begin(), channel_names.end(), "G") != channel_names.end() &&
          find(channel_names.begin(), channel_names.end(), "B") != channel_names.end()) {
        channel_names[0] = "R";
        channel_names[1] = "G";
        channel_names[2] = "B";
      }
    } else if ( m_format.planes == 4 ) {
      if (find(channel_names.begin(), channel_names.end(), "R") != channel_names.end() &&
          find(channel_names.begin(), channel_names.end(), "G") != channel_names.end() &&
          find(channel_names.begin(), channel_names.end(), "B") != channel_names.end() &&
          find(channel_names.begin(), channel_names.end(), "A") != channel_names.end()) {
        channel_names[0] = "R";
        channel_names[1] = "G";
        channel_names[2] = "B";
        channel_names[3] = "A";
      }
    }
    
    // Copy the pixels over into a ImageView object.
    ImageView<float> src_image(width, height, m_format.planes);
    Imf::FrameBuffer frameBuffer;
    for ( int32 nn = 0; nn < m_format.planes; ++nn ) {
      frameBuffer.insert (channel_names[nn].c_str(), Imf::Slice (Imf::FLOAT, (char *) (&(src_image(-bbox.min().x(),-bbox.min().y(),nn))),
                                                                 sizeof (src_image(0,0,nn)) * 1, 
                                                                 sizeof (src_image(0,0,nn)) * width, 1, 1, 0.0)); 
    }
    if (m_tiled) {
      VW_ASSERT(bbox.min().x() % m_block_size[0] == 0 && bbox.min().y() % m_block_size[1] == 0,
                ArgumentErr() << "DiskImageResourceOpenEXR: bbox corner must fall on tile boundary for read of tiled images.");
                
      static_cast<Imf::TiledInputFile*>(m_input_file_ptr)->setFrameBuffer (frameBuffer);
      int first_tile_x = bbox.min().x() / m_block_size[0];
      int first_tile_y = bbox.min().y() / m_block_size[1];
      int last_tile_x = (bbox.max().x()-1) / m_block_size[0];
      int last_tile_y = (bbox.max().y()-1) / m_block_size[1];
      static_cast<Imf::TiledInputFile*>(m_input_file_ptr)->readTiles(first_tile_x, last_tile_x, first_tile_y, last_tile_y);
    } else {
      static_cast<Imf::InputFile*>(m_input_file_ptr)->setFrameBuffer (frameBuffer);
      static_cast<Imf::InputFile*>(m_input_file_ptr)->readPixels (bbox.min().y(), std::min(int(bbox.min().y() + (height-1)), m_format.rows));
    }

    convert( dest, src_image.buffer() );
        
  } catch (Iex::BaseExc e) {
    vw_throw( vw::IOErr() << "Failed to open " << m_filename << " using the OpenEXR image reader.\n\t" << e.what() );
  } 
}

// Write the given buffer into the disk image.
void vw::DiskImageResourceOpenEXR::write( ImageBuffer const& src, BBox2i const& bbox )
{
  vw_out(VerboseDebugMessage, "fileio") << "DiskImageResourceOpenEXR: Writing OpenEXR Block " << bbox << "\n";

  if (!m_output_file_ptr) 
    vw_throw( LogicErr() << "DiskImageResourceOpenEXR: Could not write file. No file has been opened." );

  // This is pretty simple since we always write 32-bit floating point
  // files.  Note that we handle multi-channel images with interleaved
  // planes.  We've already ensured that either planes==1 or
  // channels==1.
  ImageView<float> openexr_image_block( bbox.width(), bbox.height(), m_format.planes );
  ImageBuffer dst = openexr_image_block.buffer();
  convert( dst, src );
  
  try {      
    Imf::FrameBuffer frameBuffer;
    
    // Build the framebuffer out of the various image channels 
    for (int32 nn = 0; nn < dst.format.planes; nn++) {
      frameBuffer.insert (m_labels[nn].c_str(), 
                          Imf::Slice (Imf::FLOAT, (char*) (&(openexr_image_block(-bbox.min()[0],-bbox.min()[1],nn))), 
                                      sizeof (openexr_image_block(0,0,nn)) * 1, 
                                      sizeof (openexr_image_block(0,0,nn)) * dst.format.cols));             
    } 
    
    // Write the data to disk.
    if (m_tiled) {
      VW_ASSERT(bbox.min().x() % m_block_size[0] == 0 && bbox.min().y() % m_block_size[1] == 0,
                ArgumentErr() << "DiskImageResourceOpenEXR: bbox corner must fall on tile boundary for writing of tiled images.");

      Imf::TiledOutputFile* out = static_cast<Imf::TiledOutputFile*>(m_output_file_ptr);
      out->setFrameBuffer (frameBuffer);
      int first_tile_x = bbox.min().x() / m_block_size[0];
      int first_tile_y = bbox.min().y() / m_block_size[1];
      int last_tile_x = (bbox.max().x()-1) / m_block_size[0];
      int last_tile_y = (bbox.max().y()-1) / m_block_size[1];
      out->writeTiles(first_tile_x, last_tile_x, first_tile_y, last_tile_y);
    } else {
      Imf::OutputFile* out = static_cast<Imf::OutputFile*>(m_output_file_ptr);
      out->setFrameBuffer (frameBuffer);
      out->writePixels (bbox.height());
    }

  } catch (Iex::BaseExc e) {
    vw_throw( vw::IOErr() << "DiskImageResourceOpenEXR: Failed to write " << m_filename << ".\n\t" << e.what() );
  }
  
}

// A FileIO hook to open a file for reading
vw::DiskImageResource* vw::DiskImageResourceOpenEXR::construct_open( std::string const& filename ) {
  return new DiskImageResourceOpenEXR( filename );
}

// A FileIO hook to open a file for writing
vw::DiskImageResource* vw::DiskImageResourceOpenEXR::construct_create( std::string const& filename,
                                                                       ImageFormat const& format ) {
  return new DiskImageResourceOpenEXR( filename, format );
}
