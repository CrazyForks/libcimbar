#include "unittest.h"

#include "average_hash.h"

#include "bit_file/bitmatrix.h"
#include "cimb_translator/CellDrift.h"
#include "cimb_translator/Common.h"
#include <opencv2/opencv.hpp>

#include <iostream>
#include <string>
#include <vector>

using std::string;

namespace {
	cv::Mat embedTile(const cv::Mat& tile, bool binaryThresh=false)
	{
		cv::Mat tenxten(10, 10, tile.type(), cv::Scalar(0, 0, 0));
		tile.copyTo(tenxten(cv::Rect(cv::Point(1, 1), tile.size())));

		if (binaryThresh)
		{
			cv::cvtColor(tenxten, tenxten, cv::COLOR_BGR2GRAY);
			cv::adaptiveThreshold(tenxten, tenxten, 0xFF, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 5, 0);
		}
		return tenxten;
	}

	template <typename BITSTREAM>
	void mat_to_bitbuffer(const cv::Mat& img, BITSTREAM& writer)
	{
		const uchar* p = img.ptr<uchar>(0);
		unsigned size = img.cols * img.rows;
		while (size >= 8)
		{
			const uint64_t* hax = reinterpret_cast<const uint64_t*>(p);
			uint64_t mval = (*hax) & 0x101010101010101ULL;
			const uint8_t* cv = reinterpret_cast<const uint8_t*>(&mval);
			uint8_t val = cv[0] | cv[1] << 1 | cv[2] << 2 | cv[3] << 3 | cv[4] << 4 | cv[5] << 5 | cv[6] << 6 | cv[7] << 7;
			writer << val;
			p += 8;
			size -= 8;
		}

		// remainder
		uint8_t val = 0;
		while (size > 0)
		{
			val |= (*p > 0) << size;
			++p;
			--size;
		}
		writer << val;
	}
}

TEST_CASE( "fuzzyAhashTest/testCorrectness", "[unit]" )
{
	string dir = TestCimbar::getProjectDir();
	cv::Mat tile = cimbar::getTile(4, 0, true, 0, dir);
	cv::Mat tenxten = embedTile(tile);

	// compute the hashes we expect
	std::vector<uint64_t> expected;
	for (const std::pair<int, int>& drift : CellDrift::driftPairs)
	{
		cv::Rect crop(drift.first + 1, drift.second + 1, 8, 8);
		cv::Mat img = tenxten(crop);
		expected.push_back(image_hash::average_hash(img, 64)); // we pass in a threshold value to match what fuzzy_ahash will compute
	}

	// do the real work
	auto actual = image_hash::fuzzy_ahash(tenxten);

	for (int i = 0; i < actual.size(); ++i)
		DYNAMIC_SECTION( "are we correct? : " << i )
		{
			assertEquals(expected[i], actual[i]);
		}
}

TEST_CASE( "fuzzyAhashTest/testIterator", "[unit]" )
{
	string dir = TestCimbar::getProjectDir();
	cv::Mat tile = cimbar::getTile(4, 0, true, 0, dir);
	cv::Mat tenxten = embedTile(tile);

	// compute the hashes we expect
	std::vector<uint64_t> expected;
	for (const std::pair<int, int>& drift : CellDrift::driftPairs)
	{
		cv::Rect crop(drift.first + 1, drift.second + 1, 8, 8);
		cv::Mat img = tenxten(crop);
		expected.push_back(image_hash::average_hash(img, 64)); // we pass in a threshold value to match what fuzzy_ahash will compute
	}

	// do the real work
	image_hash::ahash_result actual = image_hash::fuzzy_ahash(tenxten);
	int count = 0;
	for (auto it : actual)
	{
		++count;
		DYNAMIC_SECTION( "are we correct? : " << it.first )
		{
			assertEquals(it.second, expected[it.first]);
		}
	}
	assertEquals(9, count);

	// different range-based for loop
	count = 0;
	for (auto&& [drift_idx, hash] : actual)
	{
		++count;
		DYNAMIC_SECTION( "2nd for? : " << drift_idx )
		{
			assertEquals(hash, expected[drift_idx]);
		}
	}
	assertEquals(9, count);
}

TEST_CASE( "fuzzyAhashTest/testPreThreshold", "[unit]" )
{
	string dir = TestCimbar::getProjectDir();
	cv::Mat tile = cimbar::getTile(4, 0, true, 0, dir);
	cv::Mat tenxten = embedTile(tile, true);

	// compute the hashes we expect
	std::vector<uint64_t> expected;
	for (const std::pair<int, int>& drift : CellDrift::driftPairs)
	{
		cv::Rect crop(drift.first + 1, drift.second + 1, 8, 8);
		cv::Mat img = tenxten(crop);
		expected.push_back(image_hash::average_hash(img, 64));
	}

	// do the real work
	auto actual = image_hash::fuzzy_ahash(tenxten, 0xFE);

	for (int i = 0; i < actual.size(); ++i)
		DYNAMIC_SECTION( "are we correct? : " << i )
		{
			assertEquals(expected[i], actual[i]);
		}
}

TEST_CASE( "fuzzyAhashTest/testPreThreshold.SpecialCase", "[unit]" )
{
	string dir = TestCimbar::getProjectDir();
	cv::Mat tile = cimbar::getTile(4, 0, true, 0, dir);
	cv::Mat tenxten = embedTile(tile, true);

	// compute the hashes we expect
	std::vector<uint64_t> expected;
	for (const std::pair<int, int>& drift : CellDrift::driftPairs)
	{
		cv::Rect crop(drift.first + 1, drift.second + 1, 8, 8);
		cv::Mat img = tenxten(crop);
		expected.push_back(image_hash::average_hash(img, 64));
	}

	// do the real work
	// 0xFF is a special case. It says we *only* care about 0xFF values,
	// e.g. we can make some assumptions about the input data that might help us optimize...
	auto actual = image_hash::fuzzy_ahash(tenxten, 0xFF);

	for (int i = 0; i < actual.size(); ++i)
		DYNAMIC_SECTION( "are we correct? : " << i )
		{
			assertEquals(expected[i], actual[i]);
		}
}


TEST_CASE( "fuzzyAhashTest/testPreThreshold.BitMatrix", "[unit]" )
{
	string dir = TestCimbar::getProjectDir();
	cv::Mat tile = cimbar::getTile(4, 0, true, 0, dir);
	cv::Mat tenxten = embedTile(tile, true);

	// compute the hashes we expect
	std::vector<uint64_t> expected;
	for (const std::pair<int, int>& drift : CellDrift::driftPairs)
	{
		cv::Rect crop(drift.first + 1, drift.second + 1, 8, 8);
		cv::Mat img = tenxten(crop);
		expected.push_back(image_hash::average_hash(img, 100));
	}

	bitbuffer<> bb;
	bitbuffer<>::writer writer(bb);
	mat_to_bitbuffer(tenxten, writer);
	bitmatrix<> bm(bb, 10, 10);

	// do the real work
	auto actual = image_hash::fuzzy_ahash(bm);

	for (int i = 0; i < actual.size(); ++i)
		DYNAMIC_SECTION( "are we correct? : " << i )
		{
			assertEquals(expected[i], actual[i]);
		}
}
