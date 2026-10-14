#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

//     4:2:0 -> Y Y Y Y Cb Cr   (6 data units, MCU covers 16x16 texels)
//     4:4:4 -> Y Cb Cr         (3 data units, MCU covers  8x8  texels)


namespace jpegindex {

// MCUs per group in the MCU index, and the number of 32 bit words a group
// takes. Keep in sync with calculate_datastart() in kernels/jpeg.cu.
constexpr int MCU_INDEX_GROUP = 9;
constexpr int MCU_INDEX_WORDS = 1 + (MCU_INDEX_GROUP - 1) / 2;

constexpr int DC_BITS = 12;

// a baseline MCU has at most 10 data units, 16 leaves room
constexpr int MAX_DATA_UNITS = 16;

// Zero bytes appended to the scan: a damaged stream can make us run past the
// end, by at most one MCU, because the position is only checked once per MCU.
constexpr int SCAN_PADDING = MAX_DATA_UNITS * 248 + 8;

inline uint64_t loadBigEndian64(const uint8_t* source) {
	uint64_t value;
	std::memcpy(&value, source, 8);
#if defined(_MSC_VER)
	return _byteswap_uint64(value);
#elif defined(__GNUC__) || defined(__clang__)
	return __builtin_bswap64(value);
#else
	uint64_t swapped = 0;
	for (int i = 0; i < 8; i++) swapped |= ((value >> (8 * i)) & 0xffull) << (56 - 8 * i);
	return swapped;
#endif
}

// Reads MSB first bits. The buffer must be padded with 8 extra bytes, so that
// peeking near the end stays in bounds.
class BitScanner {
public:

	BitScanner() = default;
	BitScanner(const uint8_t* data, uint64_t numBits) : data(data), numBits(numBits) {}

	uint64_t position() const { return pos; }
	uint64_t size() const { return numBits; }
	bool atEnd() const { return pos >= numBits; }

	uint32_t peek(int count) const {
		uint64_t window = loadBigEndian64(data + (pos >> 3)) << (pos & 7);
		return uint32_t(window >> (64 - count));
	}

	uint32_t read(int count) {
		if (count == 0) return 0;
		uint32_t value = peek(count);
		pos += count;
		return value;
	}

	void skip(uint64_t count) { pos += count; }
	void alignToByte() { pos = (pos + 7) & ~uint64_t(7); }

private:
	const uint8_t* data = nullptr;
	uint64_t numBits = 0;
	uint64_t pos = 0;
};

// Writes MSB first bits into a byte buffer, 32 bits at a time.
class BitWriter {
public:

	void reserve(size_t bytes) {
		if (buffer.size() < bytes) buffer.resize(bytes);
	}

	void put(uint32_t value, int count) {
		accumulator = (accumulator << count) | (uint64_t(value) & (0xffffffffull >> (32 - count)));
		accumulatedBits += count;
		bitCount += count;
		if (accumulatedBits >= 32) emitWord();
	}


	void copy(const uint8_t* source, uint64_t sourceBit, uint32_t count) {
		while (count >= 32) {
			uint64_t window = loadBigEndian64(source + (sourceBit >> 3)) << (sourceBit & 7);
			put(uint32_t(window >> 32), 32);
			sourceBit += 32;
			count -= 32;
		}
		if (count > 0) {
			uint64_t window = loadBigEndian64(source + (sourceBit >> 3)) << (sourceBit & 7);
			put(uint32_t(window >> (64 - count)), count);
		}
	}

	uint64_t numBits() const { return bitCount; }

	std::vector<uint8_t> finish() {
		while (accumulatedBits >= 8) {
			accumulatedBits -= 8;
			ensure(1);
			buffer[used++] = uint8_t(accumulator >> accumulatedBits);
		}
		if (accumulatedBits > 0) {
			ensure(1);
			buffer[used++] = uint8_t(accumulator << (8 - accumulatedBits));
			accumulatedBits = 0;
		}

		buffer.resize(used);
		return std::move(buffer);
	}

private:

	void ensure(size_t bytes) {
		if (used + bytes > buffer.size()) buffer.resize(buffer.size() * 2 + bytes + 1024);
	}

	void emitWord() {
		accumulatedBits -= 32;
		uint32_t word = uint32_t(accumulator >> accumulatedBits);

		ensure(4);
		buffer[used + 0] = uint8_t(word >> 24);
		buffer[used + 1] = uint8_t(word >> 16);
		buffer[used + 2] = uint8_t(word >> 8);
		buffer[used + 3] = uint8_t(word >> 0);
		used += 4;
	}

	std::vector<uint8_t> buffer;
	size_t used = 0;
	uint64_t accumulator = 0;
	uint64_t bitCount = 0;
	int accumulatedBits = 0;
};

struct HuffmanSpec {
	uint8_t counts[16] = {};   // number of codes per code length
	uint8_t values[256] = {};  // symbols, ordered by code length
	int numValues = 0;
};

struct QuantSpec {
	int values[64] = {};       // in zigzag order, like in the DQT segment
};


class HuffmanDecoder {
public:

	static constexpr int LUT_BITS = 10;

	bool build(const HuffmanSpec& spec) {
		std::memset(lut, 0, sizeof(lut));
		std::memcpy(symbols, spec.values, sizeof(symbols));

		int code = 0;
		int index = 0;
		for (int length = 1; length <= 16; length++) {
			int numCodes = spec.counts[length - 1];
			minCode[length] = code;
			valueIndex[length] = index;
			maxCode[length] = numCodes > 0 ? code + numCodes - 1 : -1;

			for (int i = 0; i < numCodes; i++, code++, index++) {
				if (length <= LUT_BITS) {
					uint32_t prefix = uint32_t(code) << (LUT_BITS - length);
					uint32_t repetitions = 1u << (LUT_BITS - length);
					uint16_t entry = uint16_t((length << 8) | spec.values[index]);
					for (uint32_t suffix = 0; suffix < repetitions; suffix++) {
						lut[prefix | suffix] = entry;
					}
				}
			}

			code <<= 1;
			if (code > (1 << (length + 1))) return false; // over-subscribed table
		}

		return index == spec.numValues;
	}

	// returns the decoded symbol, or -1 if the next bits are not a valid code
	int decode(BitScanner& scanner) const {
		uint32_t window = scanner.peek(16);

		uint32_t entry = lut[window >> (16 - LUT_BITS)];
		if (entry != 0) {
			scanner.skip(entry >> 8);
			return int(entry & 0xff);
		}

		for (int length = LUT_BITS + 1; length <= 16; length++) {
			int code = int(window >> (16 - length));
			if (code <= maxCode[length]) {
				scanner.skip(length);
				return symbols[valueIndex[length] + code - minCode[length]];
			}
		}

		return -1;
	}

private:
	uint16_t lut[1 << LUT_BITS] = {};  // (code length << 8) | symbol, 0 = miss
	uint8_t symbols[256] = {};
	int minCode[17] = {};
	int maxCode[17] = {};
	int valueIndex[17] = {};
};

} // namespace jpegindex


// One indexed JPEG, ready to be handed to the GPU. Produced by indexJpeg().
struct JpegTexture {

	struct Component {
		int id = 0;
		int horizontalSampling = 1;
		int verticalSampling = 1;
		int quantTableId = 0;
		int dcTableId = 0;
		int acTableId = 0;
	};

	// one 8x8 block inside an MCU
	struct DataUnit {
		int component = 0;
		int blockX = 0;          // position inside the MCU, in blocks
		int blockY = 0;
		bool absoluteDC = false; // DC stored as DC_BITS raw bits instead of Huffman bits
	};

	// ---- image description -------------------------------------------------
	int width = 0;
	int height = 0;
	int numComponents = 0;
	int mcuWidth = 0;              // MCU size in texels
	int mcuHeight = 0;
	int mcusX = 0;                 // MCUs per row / column
	int mcusY = 0;
	int numMcus = 0;
	int dataUnitsPerMcu = 0;
	int restartInterval = 0;       // MCUs per restart interval, 0 if there are none
	int mipMapLevel = 0;           // set by the caller

	std::vector<Component> components;  // in scan order
	std::vector<DataUnit> dataUnits;    // layout of one MCU

	// ---- data --------------------------------------------------------------
	std::vector<uint8_t> mcuData;       // rewritten bit stream
	uint64_t mcuDataBits = 0;
	std::vector<uint32_t> mcuIndex;     // packed MCU bit offsets

	// [DC of component 0 .. n-1, AC of component 0 .. n-1]
	std::vector<jpegindex::HuffmanSpec> huffmanTables;
	// one per component
	std::vector<jpegindex::QuantSpec> quantTables;

	std::string error;

	int64_t dcBitsPerDataUnit[jpegindex::MAX_DATA_UNITS] = {};
	int64_t dcCountPerDataUnit[jpegindex::MAX_DATA_UNITS] = {};
	uint32_t largestRelativeOffset = 0;

	bool ok() const { return error.empty(); }

	bool is420() const { return numComponents == 3 && mcuWidth == 16 && mcuHeight == 16; }
	bool is444() const { return numComponents == 3 && mcuWidth == 8 && mcuHeight == 8; }

	const char* samplingName() const {
		if (is420()) return "4:2:0";
		if (is444()) return "4:4:4";
		if (numComponents == 1) return "grayscale";
		return "other";
	}

	// where a component's tables are in <huffmanTables>
	int dcTableIndex(int component) const { return component; }
	int acTableIndex(int component) const { return numComponents + component; }

	// same lookup as calculate_datastart() in kernels/jpeg.cu
	uint32_t mcuBitOffset(int mcu) const {
		if (mcu < 0 || mcu >= numMcus) return 0;

		int group = mcu / jpegindex::MCU_INDEX_GROUP;
		int indexInGroup = mcu % jpegindex::MCU_INDEX_GROUP;

		uint32_t absolute = mcuIndex[group * jpegindex::MCU_INDEX_WORDS];
		if (indexInGroup == 0) return absolute;

		int relativeIndex = indexInGroup - 1;
		uint32_t word = mcuIndex[group * jpegindex::MCU_INDEX_WORDS + 1 + relativeIndex / 2];
		int shift = (relativeIndex % 2 == 0) ? 16 : 0;

		return absolute + ((word >> shift) & 0xffff);
	}
};


namespace jpegindex {

class Encoder {
public:

	JpegTexture encode(const uint8_t* jpeg, size_t size);

private:

	bool parseHeaders(const uint8_t* data, size_t size);
	bool parseFrame(const uint8_t* segment, int size);
	bool parseScanHeader(const uint8_t* segment, int size);
	bool parseHuffmanTables(const uint8_t* segment, int size);
	bool parseQuantTables(const uint8_t* segment, int size);
	bool buildTables();
	bool rewriteScan(const std::vector<uint8_t>& scan, uint64_t scanBits);


	static std::vector<uint8_t> unstuff(const uint8_t* data, size_t size, uint64_t* numBits);

	bool fail(const std::string& message) {
		if (texture.error.empty()) texture.error = message;
		return false;
	}

	static int extend(uint32_t value, int category) {
		return int(value) < (1 << (category - 1)) ? int(value) - (1 << category) + 1 : int(value);
	}


	static bool skipAC(BitScanner& scanner, const HuffmanDecoder& table) {
		int k = 1;
		while (k < 64) {
			int symbol = table.decode(scanner);
			if (symbol < 0) return false;
			if (symbol == 0) break;   // end of block

			k += symbol >> 4;         // run of zeros
			if (k >= 64) break;
			scanner.skip(symbol & 15);
			k++;
		}

		return true;
	}

	void pushMcuOffset(uint64_t bitPosition) {
		if (indexInGroup == 0) {
			groupBase = bitPosition;
			indexOverflow = indexOverflow || (bitPosition > 0xffffffffull);
			texture.mcuIndex.push_back(uint32_t(bitPosition));
		} else {
			uint64_t relative = bitPosition - groupBase;
			if (relative > texture.largestRelativeOffset) texture.largestRelativeOffset = uint32_t(relative);
			indexOverflow = indexOverflow || (relative > 0xffff);

			packedWord |= uint32_t(relative & 0xffff) << (16 * (1 - packedCount));
			if (++packedCount == 2) {
				texture.mcuIndex.push_back(packedWord);
				packedWord = 0;
				packedCount = 0;
			}
		}

		if (++indexInGroup == MCU_INDEX_GROUP) finishIndexGroup();
	}

	void finishIndexGroup() {
		if (indexInGroup == 0) return;

		if (packedCount > 0) {
			texture.mcuIndex.push_back(packedWord);
			packedWord = 0;
			packedCount = 0;
		}

		while (texture.mcuIndex.size() % MCU_INDEX_WORDS != 0) texture.mcuIndex.push_back(0);
		indexInGroup = 0;
	}

	JpegTexture texture;

	HuffmanSpec huffmanSpecs[2][4];
	bool huffmanSpecDefined[2][4] = {};
	QuantSpec quantSpecs[4];
	bool quantSpecDefined[4] = {};

	std::vector<HuffmanDecoder> dcDecoders; 
	std::vector<HuffmanDecoder> acDecoders;

	size_t scanStart = 0;

	uint64_t groupBase = 0;
	uint32_t packedWord = 0;
	int packedCount = 0;
	int indexInGroup = 0;
	bool indexOverflow = false;
};


inline JpegTexture Encoder::encode(const uint8_t* jpeg, size_t size) {

	if (jpeg == nullptr || size < 4) {
		fail("indexJpeg: empty input");
		return std::move(texture);
	}

	if (parseHeaders(jpeg, size) && buildTables()) {
		uint64_t scanBits = 0;
		std::vector<uint8_t> scan = unstuff(jpeg + scanStart, size - scanStart, &scanBits);

		rewriteScan(scan, scanBits);
	}

	if (!texture.ok()) {
		texture.mcuData.clear();
		texture.mcuIndex.clear();
	}

	return std::move(texture);
}

inline bool Encoder::parseHeaders(const uint8_t* data, size_t size) {

	if (data[0] != 0xff || data[1] != 0xd8) return fail("indexJpeg: not a JPEG file");

	size_t index = 2;
	while (index + 1 < size) {

		if (data[index] != 0xff) return fail("indexJpeg: expected a marker");
		while (index < size && data[index] == 0xff) index++; // fill bytes
		if (index >= size) return fail("indexJpeg: truncated file");

		uint8_t marker = data[index++];

		// markers without a payload
		if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
		if (marker == 0xd9) return fail("indexJpeg: no scan found");

		if (index + 2 > size) return fail("indexJpeg: truncated segment");
		int length = (data[index] << 8) | data[index + 1];
		if (length < 2 || index + length > size) return fail("indexJpeg: invalid segment length");

		const uint8_t* segment = data + index + 2;
		int segmentSize = length - 2;

		switch (marker) {
		case 0xc0: // baseline
		case 0xc1: // extended sequential, same entropy coding
			if (!parseFrame(segment, segmentSize)) return false;
			break;
		case 0xc2:
			return fail("indexJpeg: progressive JPEGs are not supported");
		case 0xc4:
			if (!parseHuffmanTables(segment, segmentSize)) return false;
			break;
		case 0xcc:
			return fail("indexJpeg: arithmetic coding is not supported");
		case 0xdb:
			if (!parseQuantTables(segment, segmentSize)) return false;
			break;
		case 0xdd:
			if (segmentSize < 2) return fail("indexJpeg: invalid DRI segment");
			texture.restartInterval = (segment[0] << 8) | segment[1];
			break;
		case 0xda:
			if (!parseScanHeader(segment, segmentSize)) return false;
			scanStart = index + length;
			return true;
		default:
			if (marker >= 0xc3 && marker <= 0xcf && marker != 0xc8) {
				return fail("indexJpeg: unsupported SOF marker " + std::to_string(int(marker)));
			}
			break; // APPn, COM, ... - skipped
		}

		index += length;
	}

	return fail("indexJpeg: no scan found");
}

inline bool Encoder::parseFrame(const uint8_t* segment, int size) {

	if (size < 6) return fail("indexJpeg: invalid SOF segment");

	int precision = segment[0];
	if (precision != 8) return fail("indexJpeg: only 8 bit samples are supported");

	texture.height = (segment[1] << 8) | segment[2];
	texture.width = (segment[3] << 8) | segment[4];
	texture.numComponents = segment[5];

	if (texture.width <= 0 || texture.height <= 0) return fail("indexJpeg: invalid image size");
	if (texture.numComponents < 1 || texture.numComponents > 4) return fail("indexJpeg: invalid component count");
	if (size < 6 + texture.numComponents * 3) return fail("indexJpeg: invalid SOF segment");

	texture.components.clear();
	int maxHorizontal = 1;
	int maxVertical = 1;

	for (int i = 0; i < texture.numComponents; i++) {
		const uint8_t* entry = segment + 6 + i * 3;

		JpegTexture::Component component;
		component.id = entry[0];
		component.horizontalSampling = (entry[1] >> 4) & 0xf;
		component.verticalSampling = entry[1] & 0xf;
		component.quantTableId = entry[2];

		if (component.horizontalSampling < 1 || component.horizontalSampling > 4) return fail("indexJpeg: invalid sampling factor");
		if (component.verticalSampling < 1 || component.verticalSampling > 4) return fail("indexJpeg: invalid sampling factor");
		if (component.quantTableId > 3) return fail("indexJpeg: invalid quantization table id");

		maxHorizontal = std::max(maxHorizontal, component.horizontalSampling);
		maxVertical = std::max(maxVertical, component.verticalSampling);

		texture.components.push_back(component);
	}

	texture.mcuWidth = 8 * maxHorizontal;
	texture.mcuHeight = 8 * maxVertical;
	texture.mcusX = (texture.width + texture.mcuWidth - 1) / texture.mcuWidth;
	texture.mcusY = (texture.height + texture.mcuHeight - 1) / texture.mcuHeight;
	texture.numMcus = texture.mcusX * texture.mcusY;

	return true;
}

inline bool Encoder::parseScanHeader(const uint8_t* segment, int size) {

	if (texture.components.empty()) return fail("indexJpeg: scan before frame header");
	if (size < 1) return fail("indexJpeg: invalid SOS segment");

	int numScanComponents = segment[0];
	if (numScanComponents != texture.numComponents) {
		return fail("indexJpeg: only single scan, fully interleaved JPEGs are supported");
	}
	if (size < 1 + numScanComponents * 2 + 3) return fail("indexJpeg: invalid SOS segment");

	std::vector<JpegTexture::Component> scanOrder;
	for (int i = 0; i < numScanComponents; i++) {
		int componentId = segment[1 + i * 2];
		int tables = segment[2 + i * 2];

		const JpegTexture::Component* found = nullptr;
		for (const JpegTexture::Component& component : texture.components) {
			if (component.id == componentId) found = &component;
		}
		if (found == nullptr) return fail("indexJpeg: scan references an unknown component");

		JpegTexture::Component component = *found;
		component.dcTableId = (tables >> 4) & 0xf;
		component.acTableId = tables & 0xf;
		if (component.dcTableId > 3 || component.acTableId > 3) return fail("indexJpeg: invalid Huffman table id");

		scanOrder.push_back(component);
	}
	texture.components = scanOrder;

	const uint8_t* spectral = segment + 1 + numScanComponents * 2;
	if (spectral[0] != 0 || spectral[1] != 63 || spectral[2] != 0) {
		return fail("indexJpeg: only baseline scans are supported");
	}

	// data unit layout of one MCU, in interleave order
	texture.dataUnits.clear();
	for (int c = 0; c < texture.numComponents; c++) {
		const JpegTexture::Component& component = texture.components[c];
		for (int y = 0; y < component.verticalSampling; y++) {
			for (int x = 0; x < component.horizontalSampling; x++) {
				JpegTexture::DataUnit unit;
				unit.component = c;
				unit.blockX = x;
				unit.blockY = y;
				unit.absoluteDC = (x == 0 && y == 0);
				texture.dataUnits.push_back(unit);
			}
		}
	}
	texture.dataUnitsPerMcu = int(texture.dataUnits.size());

	if (texture.dataUnitsPerMcu > MAX_DATA_UNITS) {
		return fail("indexJpeg: too many data units per MCU");
	}

	return true;
}

inline bool Encoder::parseHuffmanTables(const uint8_t* segment, int size) {

	int index = 0;
	while (index < size) {
		if (index + 17 > size) return fail("indexJpeg: invalid DHT segment");

		int tableClass = (segment[index] >> 4) & 0xf;
		int tableId = segment[index] & 0xf;
		index++;

		if (tableClass > 1 || tableId > 3) return fail("indexJpeg: invalid Huffman table id");

		HuffmanSpec spec;
		int numValues = 0;
		for (int i = 0; i < 16; i++) {
			spec.counts[i] = segment[index + i];
			numValues += spec.counts[i];
		}
		index += 16;

		if (numValues > 256) return fail("indexJpeg: invalid Huffman table");
		if (index + numValues > size) return fail("indexJpeg: invalid DHT segment");

		std::memcpy(spec.values, segment + index, numValues);
		spec.numValues = numValues;
		index += numValues;

		huffmanSpecs[tableClass][tableId] = spec;
		huffmanSpecDefined[tableClass][tableId] = true;
	}

	return true;
}

inline bool Encoder::parseQuantTables(const uint8_t* segment, int size) {

	int index = 0;
	while (index < size) {
		int precision = (segment[index] >> 4) & 0xf;
		int tableId = segment[index] & 0xf;
		index++;

		if (tableId > 3) return fail("indexJpeg: invalid quantization table id");
		if (precision > 1) return fail("indexJpeg: invalid quantization table precision");

		int tableSize = precision == 0 ? 64 : 128;
		if (index + tableSize > size) return fail("indexJpeg: invalid DQT segment");

		QuantSpec spec;
		for (int i = 0; i < 64; i++) {
			spec.values[i] = precision == 0
				? segment[index + i]
				: (segment[index + i * 2] << 8) | segment[index + i * 2 + 1];
		}
		index += tableSize;

		quantSpecs[tableId] = spec;
		quantSpecDefined[tableId] = true;
	}

	return true;
}

inline bool Encoder::buildTables() {

	dcDecoders.resize(texture.numComponents);
	acDecoders.resize(texture.numComponents);

	for (int tableClass = 0; tableClass < 2; tableClass++) {
		for (int c = 0; c < texture.numComponents; c++) {
			int tableId = tableClass == 0 ? texture.components[c].dcTableId : texture.components[c].acTableId;
			if (!huffmanSpecDefined[tableClass][tableId]) {
				return fail("indexJpeg: scan uses an undefined Huffman table");
			}

			const HuffmanSpec& spec = huffmanSpecs[tableClass][tableId];
			HuffmanDecoder& decoder = tableClass == 0 ? dcDecoders[c] : acDecoders[c];
			if (!decoder.build(spec)) return fail("indexJpeg: invalid Huffman table");

			texture.huffmanTables.push_back(spec);
		}
	}

	for (int c = 0; c < texture.numComponents; c++) {
		int tableId = texture.components[c].quantTableId;
		if (!quantSpecDefined[tableId]) return fail("indexJpeg: scan uses an undefined quantization table");

		texture.quantTables.push_back(quantSpecs[tableId]);
	}

	return true;
}

inline std::vector<uint8_t> Encoder::unstuff(const uint8_t* data, size_t size, uint64_t* numBits) {

	std::vector<uint8_t> out;
	out.reserve(size + SCAN_PADDING);

	size_t index = 0;
	while (index < size) {
		const uint8_t* hit = (const uint8_t*)std::memchr(data + index, 0xff, size - index);
		if (hit == nullptr) {
			out.insert(out.end(), data + index, data + size);
			break;
		}

		size_t position = size_t(hit - data);
		out.insert(out.end(), data + index, data + position);

		if (position + 1 >= size) break;

		uint8_t next = data[position + 1];
		if (next == 0x00) {
			out.push_back(0xff);        // stuffed 0xff
			index = position + 2;
		} else if (next == 0xff) {
			index = position + 1;       // fill byte
		} else if (next >= 0xd0 && next <= 0xd7) {
			index = position + 2;       // restart marker, dropped
		} else {
			break;                      // EOI or the next segment ends the scan
		}
	}

	*numBits = uint64_t(out.size()) * 8;
	out.resize(out.size() + SCAN_PADDING, 0);

	return out;
}

inline bool Encoder::rewriteScan(const std::vector<uint8_t>& scan, uint64_t scanBits) {

	BitScanner scanner(scan.data(), scanBits);

	BitWriter writer;
	writer.reserve(scan.size() + size_t(texture.numMcus) * texture.numComponents * 2 + 1024);

	texture.mcuIndex.reserve(size_t((texture.numMcus + MCU_INDEX_GROUP - 1) / MCU_INDEX_GROUP) * MCU_INDEX_WORDS);

	std::vector<int> predictions(texture.numComponents, 0);
	uint64_t acStart[MAX_DATA_UNITS] = {};
	uint32_t acBits[MAX_DATA_UNITS] = {};

	for (int mcu = 0; mcu < texture.numMcus; mcu++) {

		if (texture.restartInterval > 0 && mcu > 0 && mcu % texture.restartInterval == 0) {
			scanner.alignToByte();
			std::fill(predictions.begin(), predictions.end(), 0);
		}

		pushMcuOffset(writer.numBits());

		for (int du = 0; du < texture.dataUnitsPerMcu; du++) {
			const JpegTexture::DataUnit& unit = texture.dataUnits[du];

			uint64_t dcStart = scanner.position();
			int category = dcDecoders[unit.component].decode(scanner);
			if (category < 0 || category > 15) {
				return fail("indexJpeg: invalid DC code in MCU " + std::to_string(mcu));
			}
			predictions[unit.component] += category > 0 ? extend(scanner.read(category), category) : 0;

			uint32_t dcBitCount = uint32_t(scanner.position() - dcStart);
			texture.dcBitsPerDataUnit[du] += dcBitCount;
			texture.dcCountPerDataUnit[du] += 1;

			if (unit.absoluteDC) {
				writer.put(uint32_t(predictions[unit.component] + 2048) & 0xfff, DC_BITS);
			} else {
				writer.copy(scan.data(), dcStart, dcBitCount);
			}

			acStart[du] = scanner.position();
			if (!skipAC(scanner, acDecoders[unit.component])) {
				return fail("indexJpeg: invalid AC code in MCU " + std::to_string(mcu));
			}
			acBits[du] = uint32_t(scanner.position() - acStart[du]);
		}

		for (int du = 0; du < texture.dataUnitsPerMcu; du++) {
			writer.copy(scan.data(), acStart[du], acBits[du]);
		}

		if (scanner.position() > scanBits) {
			return fail("indexJpeg: scan data ends inside MCU " + std::to_string(mcu));
		}
	}

	finishIndexGroup();

	if (indexOverflow) return fail("indexJpeg: MCU offsets do not fit into the index");

	texture.mcuDataBits = writer.numBits();
	texture.mcuData = writer.finish();

	return true;
}

}
inline JpegTexture indexJpeg(const uint8_t* jpeg, size_t size) {
	return jpegindex::Encoder().encode(jpeg, size);
}

inline JpegTexture indexJpeg(const std::vector<uint8_t>& jpeg) {
	return indexJpeg(jpeg.data(), jpeg.size());
}
