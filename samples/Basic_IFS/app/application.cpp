/*
 * Webserver demo using IFS
 *
 */

#include <SmingCore.h>
#include <Data/Stream/MemoryDataStream.h>
#include <Data/Stream/IFS/DirectoryTemplate.h>
#include <Data/Stream/IFS/HtmlDirectoryTemplate.h>
#include <Data/Stream/IFS/JsonDirectoryTemplate.h>
#include <Data/Stream/IFS/ArchiveStream.h>
#include <Storage/ProgMem.h>
#include <LittleFS.h>
#include <Services/Profiling/TaskStat.h>

// If you want, you can define WiFi settings globally in Eclipse Environment Variables
#ifndef WIFI_SSID
#define WIFI_SSID "PleaseEnterSSID" // Put your SSID and password here
#define WIFI_PWD "PleaseEnterPass"
#endif

#ifdef ENABLE_SDCARD
#include <Storage/SD/Card.h>
#include <IFS/FAT.h>

// Chip selects independent of SPI controller in use
#ifdef ARCH_ESP32
#define PIN_CARD_CS 21
#else
// Esp8266 cannot use GPIO15 as this affects boot mode
#define PIN_CARD_CS 5
#endif

#define SPI_FREQ_LIMIT 0 //2000000

#endif

#ifdef ENABLE_USB_STORAGE
#include <USB.h>
#include <IFS/FAT.h>
USB::MSC::HostDevice usbStorage;
#endif

namespace
{
#ifdef ENABLE_FLASHSTRING_IMAGE
IMPORT_FSTR(fwfsImage, PROJECT_DIR "/out/fwfs1.bin")
#endif

IMPORT_FSTR(listing_html, PROJECT_DIR "/resource/listing.html")
IMPORT_FSTR(listing_txt, PROJECT_DIR "/resource/listing.txt")
IMPORT_FSTR(listing_json, PROJECT_DIR "/resource/listing.json")

HttpServer server;
FtpServer ftp;
int requestCount;
Profiling::TaskStat taskStat(Serial);
SimpleTimer statTimer;

/*
 * Handle any custom fields here
 */
String getValue(const char* name)
{
	if(FS("webpage") == name) {
		return "https://github.com/SmingHub/Sming";
	}

	if(FS("request-count") == name) {
		return String(requestCount); // Doesn't require escaping
	}

	return nullptr;
}

void onFile(HttpRequest& request, HttpResponse& response)
{
	++requestCount;

	String file = request.uri.getRelativePath();
	String fmt = request.uri.getQueryParameter("format");

	if(dirExist(file)) {
		if(fmt.equalsIgnoreCase("archive")) {
			debug_i("Sending streaming archive");
			IFS::FileSystem::NameInfo fsinfo;
			fileGetSystemInfo(fsinfo);
			ArchiveStream::VolumeInfo volumeInfo;
			volumeInfo.name = F("Backup of '") + fsinfo.name + "'";
			if(file.length() != 0) {
				volumeInfo.name += F("; root = '") + file + "'";
			}
			auto archive = new ArchiveStream(volumeInfo, file, ArchiveStream::Flag::IncludeMountPoints);
			response.sendDataStream(archive, archive->getMimeType());
			return;
		}

		auto dir = new Directory;
		IFS::DirectoryTemplate* tmpl;
		if(fmt.equalsIgnoreCase("json")) {
			auto source = new FlashMemoryStream(listing_json);
			tmpl = new IFS::JsonDirectoryTemplate(source, dir);
		} else if(fmt.equalsIgnoreCase("text")) {
			auto source = new FlashMemoryStream(listing_txt);
			tmpl = new IFS::DirectoryTemplate(source, dir);
		} else {
			auto source = new FlashMemoryStream(listing_html);
			tmpl = new IFS::HtmlDirectoryTemplate(source, dir);
		}
		tmpl->onGetValue(getValue);
		dir->open(file);
		tmpl->gotoSection(0);
		response.sendDataStream(tmpl, tmpl->getMimeType());
		return;
	}

	if(fmt) {
		debug_e("'format' option only supported for directories");
		response.code = HTTP_STATUS_BAD_REQUEST;
		return;
	}

	//	response.setCache(86400, true); // It's important to use cache for better performance.
	auto stream = new FileStream;
	if(!stream->open(file)) {
		int err = stream->getLastError();
		response.code = (err == IFS::Error::NotFound) ? HTTP_STATUS_NOT_FOUND : HTTP_STATUS_INTERNAL_SERVER_ERROR;
		delete stream;
		return;
	}
	FileStat stat;
	stream->stat(stat);
	if(stat.compression.type == IFS::Compression::Type::GZip) {
		response.headers[HTTP_HEADER_CONTENT_ENCODING] = F("gzip");
	} else if(stat.compression.type != IFS::Compression::Type::None) {
		debug_e("Unsupported compression type: %u", stat.compression.type);
	}
	auto mimeType = ContentType::fromFullFileName(file, MIME_TEXT);
	response.sendDataStream(stream, mimeType);
}

void startWebServer()
{
	server.listen(80);
	server.paths.setDefault(onFile);

	Serial.println("\r\n=== WEB SERVER STARTED ===");
	Serial.println(WifiStation.getIP());
	Serial.println("==============================\r\n");
}

void gotIP(IpAddress ip, IpAddress netmask, IpAddress gateway)
{
	startWebServer();
}

bool initFileSystem()
{
	fileFreeFileSystem();

	auto initialFreeheap = system_get_free_heap_size();
	debug_i("Initial freeheap = %u", initialFreeheap);

#ifdef ENABLE_FLASHSTRING_IMAGE
	// Create a partition wrapping some flashstring data
	auto part =
		Storage::progMem.editablePartitions().add(F("fwfsMem"), fwfsImage, Storage::Partition::SubType::Data::fwfs);
#else
	auto part = Storage::findDefaultPartition(Storage::Partition::SubType::Data::fwfs);
#endif

	// Read-only
	auto fs = IFS::createFirmwareFilesystem(part);

	if(fs == nullptr) {
		debug_e("Failed to created filesystem object");
		return false;
	}

	auto mount = [&](IFS::FileSystem* fs) {
		int res = fs->mount();
		debug_i("heap used: %u, mount() returned %d (%s)", initialFreeheap - system_get_free_heap_size(), res,
				fs->getErrorString(res).c_str());
		return res == FS_OK;
	};

	if(!mount(fs)) {
		delete fs;
		return false;
	}

	// Make this the default filesystem
	fileSetFileSystem(fs);

	// Let's mount an LFS volume as well
	initialFreeheap = system_get_free_heap_size();
	part = Storage::findDefaultPartition(Storage::Partition::SubType::Data::littlefs);
	auto lfs = IFS::createLfsFilesystem(part);
	if(lfs == nullptr) {
		debug_e("Failed to create LFS filesystem");
	} else if(mount(lfs)) {
		// Place the root of this volume at index #0 (the corresponding directory is given in `fwimage.fwfs`)
		fs->setVolume(0, lfs);
	} else {
		delete lfs;
	}

	// And we'll mount a SPIFFS volume too
	initialFreeheap = system_get_free_heap_size();
	part = Storage::findDefaultPartition(Storage::Partition::SubType::Data::spiffs);
	auto spiffs = IFS::createSpiffsFilesystem(part);
	if(spiffs == nullptr) {
		debug_e("Failed to create SPIFFS filesystem");
	} else if(mount(spiffs)) {
		// Place the root of this volume at index #1
		fs->setVolume(1, spiffs);
	} else {
		delete spiffs;
	}

#ifdef ENABLE_SDCARD
	auto card = new Storage::SD::Card("card1", SPI);
	Storage::registerDevice(card);

	// Buffering allows byte read/write
	card->allocateBuffers(2);

	if(card->begin(PIN_CARD_CS, SPI_FREQ_LIMIT)) {
		Serial << "CSD" << endl << card->csd << endl;
		Serial << "CID" << endl << card->cid;

		auto part = *card->partitions().begin();
		auto fatfs = IFS::createFatFilesystem(part);
		if(fatfs != nullptr) {
			if(fatfs->mount() == FS_OK) {
				fs->setVolume(2, fatfs);
			} else {
				delete fatfs;
				delete card;
			}
		}
	} else {
		delete card;
	}

#endif

#ifdef ENABLE_USB_STORAGE
	USB::begin(true);
	USB::MSC::onMount([](auto inst) {
		usbStorage.begin(inst);
		usbStorage.enumerate([](auto& unit, const USB::MSC::Inquiry& inquiry) {
#define OUT(name, value) Serial << String(name).padLeft(30) << ": " << value << endl;
#define OUTR(name) OUT(#name, inquiry.resp.name)
			Serial << _F("USB device '") << unit.getName() << _F("' mounted") << endl;
			OUT("Vendor ID", inquiry.vendorId())
			OUT("Product ID", inquiry.productId())
			OUT("Product Revision", inquiry.productRev())
			OUTR(peripheral_device_type)
			OUTR(peripheral_qualifier)
			OUTR(is_removable)
			OUTR(version)
			OUTR(response_data_format)
			OUTR(hierarchical_support)
			OUTR(normal_aca)
			OUTR(additional_length)
			OUTR(protect)
			OUTR(third_party_copy)
			OUTR(target_port_group_support)
			OUTR(access_control_coordinator)
			OUTR(scc_support)
			OUTR(addr16)
			OUTR(multi_port)
			OUTR(enclosure_service)
			OUTR(cmd_que)
			OUTR(sync)
			OUTR(wbus16)
#undef OUTR
#undef OUT

			Storage::registerDevice(&unit);
			unit.allocateBuffers(16);

			for(auto part : unit.partitions()) {
				Serial << part << endl;
			}
			auto part = Storage::findDefaultPartition(Storage::Partition::SubType::Data::fat);
			auto fatfs = IFS::createFatFilesystem(part);
			if(fatfs && fatfs->mount() == FS_OK) {
				getFileSystem()->setVolume(3, fatfs);
				Serial << F("FAT partition mounted") << endl;
			} else {
				Serial << F("FAT mount failed") << endl;
				delete fatfs;
			}

			return false; // Ignore other LUNs
		});

		return &usbStorage;
	});

	USB::MSC::onUnmount([](USB::MSC::HostDevice& dev) {
		if(dev == usbStorage) {
			getFileSystem()->setVolume(3, nullptr);
			Serial << _F("USB '") << dev.getName() << _F("' unmounted") << endl;
		}
	});
#endif

	debug_i("File system initialised");
	return true;
}

void printDirectory(const char* path)
{
	auto printStream = [](IDataSourceStream& stream) {
		// Use an intermediate memory stream so debug information doesn't get mixed into output
		//		MemoryDataStream mem;
		//		mem.copyFrom(&stream);
		//		Serial.copyFrom(&mem);
		Serial.copyFrom(&stream);
	};

	{
		auto dir = new Directory;
		if(!dir->open(path)) {
			debug_e("Open '%s' failed: %s", path, dir->getLastErrorString().c_str());
			delete dir;
			return;
		}

		auto source = new FlashMemoryStream(listing_txt);
		IFS::DirectoryTemplate tmpl(source, dir);
		printStream(tmpl);
	}

	{
		auto dir = new Directory;
		if(!dir->open(path)) {
			debug_e("Open '%s' failed: %s", path, dir->getLastErrorString().c_str());
			delete dir;
			return;
		}

		auto source = new FlashMemoryStream(listing_json);
		IFS::JsonDirectoryTemplate tmpl(source, dir);
		printStream(tmpl);
	}
}

void copySomeFiles()
{
	auto part = *Storage::findPartition(Storage::Partition::SubType::Data::fwfs);
	if(!part) {
		return;
	}
	auto fs = IFS::createFirmwareFilesystem(part);
	if(fs == nullptr) {
		return;
	}
	fs->mount();

	IFS::Directory dir(fs);
	if(!dir.open()) {
		return;
	}

	while(dir.next()) {
		auto& stat = dir.stat();
		if(stat.isDir()) {
			continue;
		}
		IFS::File src(fs);
		auto filename = stat.name.c_str();
		if(src.open(filename)) {
			File dst;
			if(dst.open(filename, File::CreateNewAlways | File::WriteOnly)) {
				auto len =
					src.readContent([&dst](const char* buffer, size_t size) -> int { return dst.write(buffer, size); });
				(void)len;
				debug_w("Wrote '%s', %d bytes", filename, len);

				// Copy metadata
				auto callback = [&](IFS::AttributeEnum& e) -> bool {
					if(!dst.setAttribute(e.tag, e.buffer, e.size)) {
						m_printf(_F("setAttribute(%s) failed: %s"), toString(e.tag).c_str(),
								 dst.getLastErrorString().c_str());
					}
					return true;
				};
				char buffer[1024];
				src.enumAttributes(callback, buffer, sizeof(buffer));
			} else {
				debug_w("%s", dst.getLastErrorString().c_str());
			}
		}
	}
}

bool isVolumeEmpty()
{
	Directory dir;
	dir.open();
	return !dir.next();
}

void listAttributes()
{
	Directory dir;
	if(dir.open()) {
		while(dir.next()) {
			auto filename = dir.stat().name.c_str();
			File f;
			if(!f.open(filename)) {
				continue;
			}
			m_printf("%s:\r\n", filename);
			auto callback = [](IFS::AttributeEnum& e) -> bool {
				m_printf("  attr 0x%04x %s, %u bytes\r\n", unsigned(e.tag), toString(e.tag).c_str(), e.attrsize);
				m_printHex("  ATTR", e.buffer, e.size);
				return true;
			};
			char buffer[64];
			int res = f.enumAttributes(callback, buffer, sizeof(buffer));
			debug_i("res: %d", res);
		}
	}
}

void fstest()
{
	// Various ways to initialise a filesystem

	/*
	 * Mount regular SPIFFS volume
	 */
	// spiffs_mount();

	/*
	 * Mount LittleFS volume
	 */
	// lfs_mount();

	/*
	 * Mount default Firmware Filesystem
	 */
	// fwfs_mount();

	/*
	 * Mount default FWFS/SPIFFS as hybrid
	 */
	// hyfs_mount();

	/*
	 * Explore some alternative methods of mounting filesystems
	 */
	initFileSystem();

	if(isVolumeEmpty()) {
		Serial.print(F("Volume appears to be empty, writing some files...\r\n"));
		copySomeFiles();
	}

	printDirectory(nullptr);

	listAttributes();
}

} // namespace

void init()
{
	Serial.begin(COM_SPEED_SERIAL);

	Serial.systemDebugOutput(true);
	debug_i("\n\n********************************************************\n"
			"Hello\n");

	// Delay at startup so terminal gets time to start
	auto timer = new AutoDeleteTimer;
	timer->initializeMs<1000>(fstest);
	timer->startOnce();

	WifiStation.enable(true);
	WifiStation.config(WIFI_SSID, WIFI_PWD);
	WifiAccessPoint.enable(false);

	WifiEvents.onStationGotIP(gotIP);

	statTimer.initializeMs<2000>([]() { taskStat.update(); });
	statTimer.start();
}
