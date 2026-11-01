#include "DDImage/Hash.h"
#include "DDImage/Knobs.h"
#include "DDImage/SourceGeomOp.h"

#include "NukeReaderSession.h"

#include "ndk/geo/utils/MeshUtils.h"
#include "usg/geom/MeshPrim.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace openvolumetric::nuke
{
namespace
{
constexpr const char* kNodeClass = "OpenVolumetricGeo";
constexpr const char* kNodeHelp =
	"Reads animated geometry from the vvge track of one OpenVolumetric MP4. "
	"The same file also contains the synchronized texture and optional audio.";
constexpr const char* kFileKnob = "file";
constexpr const char* kStartFrameKnob = "start_frame";
constexpr const char* kFrameOffsetKnob = "frame_offset";
constexpr const char* kSourceFrameRateKnob = "source_frame_rate";
constexpr const char* kTrimOutKnob = "trim_out";
constexpr const char* kBeforeModeKnob = "before_mode";
constexpr const char* kAfterModeKnob = "after_mode";
constexpr const char* kGeometryScaleKnob = "geometry_scale";
constexpr const char* kCacheFramesKnob = "cache_frames";
constexpr const char* kCacheMegabytesKnob = "cache_megabytes";
constexpr const char* kReloadVersionKnob = "reload_version";

class OpenVolumetricGeo final : public DD::Image::SourceGeomOp
{
public:
	class Engine final : public SourceEngine
	{
	public:
		explicit Engine(DD::Image::GeomOpNode* parent)
			: SourceEngine(parent)
		{
		}

		void createPrims(
			usg::GeomSceneContext& context,
			const usg::Path& path) override
		{
			// Nuke can issue overlapping background evaluations while the user
			// scrubs. The reader itself is thread-safe, but editLayer() and its
			// MeshPrim authoring operations belong to this engine and must not be
			// mutated concurrently.
			std::lock_guard<std::mutex> evaluation_lock(m_evaluation_mutex);
			if (!context.doGeometryProcessing())
				return;

			usg::LayerRef layer = editLayer();
			if (!layer)
				return;

			const auto file = knob(kFileKnob);
			const std::string source = file ? file.get<std::string>() : "";
			std::string session_error;
			const int cache_frames = std::max(
				0, knob(kCacheFramesKnob).get<int>());
			const int cache_megabytes = std::max(
				0, knob(kCacheMegabytesKnob).get<int>());
			auto session = NukeReaderSession::acquire(
				source,
				static_cast<std::size_t>(cache_frames),
				static_cast<std::size_t>(cache_megabytes),
				knob(kReloadVersionKnob).get<int>(),
				session_error);
			if (!session)
			{
				firstOp()->error("OpenVolumetric: %s", session_error.c_str());
				return;
			}
			// Retain the registry session for this engine's lifetime so the paired
			// texture node can acquire the same presentation cache between calls.
			m_session = session;

			const OpenVolumetricMediaInfo info = session->media_info();
			const double final_source_frame = std::max(
				0.0, std::ceil(info.duration * info.frame_rate) - 1.0);
			const double configured_trim_out = knob(kTrimOutKnob).get<double>();
			const double trim_in = std::clamp(
				knob(kFrameOffsetKnob).get<double>(), 0.0, final_source_frame);
			const double trim_out = configured_trim_out >= 0.0
				? std::clamp(configured_trim_out, trim_in, final_source_frame)
				: final_source_frame;
			const bool hold_before = knob(kBeforeModeKnob).get<int>() != 0;
			const bool hold_after = knob(kAfterModeKnob).get<int>() != 0;

			struct TimedPresentation
			{
				fdk::TimeValue time;
				double scale;
				std::shared_ptr<const OpenVolumetricPresentation> presentation;
			};
			std::vector<TimedPresentation> presentations;

			for (const fdk::TimeValue time : context.processTimes())
			{
				const double requested_rate =
					knob(kSourceFrameRateKnob).get<double>(time);
				const double source_rate = requested_rate > 0.0
					? requested_rate
					: info.frame_rate;
				if (!std::isfinite(source_rate) || source_rate <= 0.0)
				{
					firstOp()->error(
						"OpenVolumetric: source frame rate must be positive.");
					return;
				}

				const double start_frame =
					static_cast<double>(knob(kStartFrameKnob).get<int>(time));
				const double frame_offset = knob(kFrameOffsetKnob).get<double>(time);
				const double requested_source_frame =
					time - start_frame + frame_offset;
				if ((requested_source_frame < trim_in && !hold_before) ||
					(requested_source_frame > trim_out && !hold_after))
					continue;
				const double source_frame = std::clamp(
					requested_source_frame, trim_in, trim_out);
				const double presentation_time = source_frame / source_rate;

				std::shared_ptr<const OpenVolumetricPresentation> presentation;
				const FrameRequestResult result =
					session->request(presentation_time, presentation);
				if (result != FrameRequestResult::Ready || !presentation)
				{
					const std::string detail = session->error();
					firstOp()->error(
						"OpenVolumetric: %s%s%s",
						request_result_message(result),
						detail.empty() ? "" : " ",
						detail.c_str());
					return;
				}

				presentations.push_back({
					time,
					knob(kGeometryScaleKnob).get<double>(time),
					std::move(presentation)});
			}

			// For the `none` boundary mode, do not define a partial primitive.
			// This is important because Hydra may attempt to render any defined
			// mesh even when no complete point/topology sample was published.
			if (presentations.empty())
				return;

			usg::MeshPrim mesh = usg::MeshPrim::defineInLayer(layer, path);
			_transformSubEngine.apply(context, mesh);
			bool build_topology = context.doGeometryInitialization();
			for (const TimedPresentation& item : presentations)
			{
				if (!publish_mesh(
					mesh,
					item.time,
					item.scale,
					item.presentation->mesh,
					build_topology))
				{
					firstOp()->error(
						"OpenVolumetric: decoded mesh has invalid triangle indices.");
					return;
				}
				build_topology = false;
			}

			assignMaterial(context, {path});
		}

	private:
		/// Converts core clockwise triangles into Nuke-natural CCW faces.
		static bool publish_mesh(
			usg::MeshPrim& destination,
			fdk::TimeValue time,
			double scale,
			const Mesh& source,
			bool build_topology)
		{
			if (source.indexes.size() % 3 != 0)
				return false;

			ndk::MeshSample sample;
			sample.time = time;
			sample.points.reserve(source.verts.size());
			for (const Vertex& vertex : source.verts)
			{
				sample.points.emplace_back(
					static_cast<float>(vertex.pos[0] * scale),
					static_cast<float>(vertex.pos[1] * scale),
					static_cast<float>(vertex.pos[2] * scale));
			}

			const std::size_t face_count = source.indexes.size() / 3;
			sample.reserveFaceVertSpace(face_count, source.indexes.size());
			sample.facevert_uvs.reserve(source.indexes.size());
			sample.facevert_normals.reserve(source.indexes.size());
			for (std::size_t face = 0; face < face_count; ++face)
			{
				const int core_indices[3] = {
					source.indexes[face * 3],
					source.indexes[face * 3 + 2],
					source.indexes[face * 3 + 1],
				};
				for (const int index : core_indices)
				{
					if (index < 0 ||
						static_cast<std::size_t>(index) >= source.verts.size())
						return false;
				}
				sample.addTri(core_indices[0], core_indices[1], core_indices[2]);
				for (const int index : core_indices)
				{
					const Vertex& vertex = source.verts[
						static_cast<std::size_t>(index)];
					sample.facevert_uvs.emplace_back(vertex.uv[0], vertex.uv[1]);
					sample.facevert_normals.emplace_back(
						vertex.normal[0], vertex.normal[1], vertex.normal[2]);
				}
			}

			if (build_topology)
				sample.setMeshPrimFaceTopology(destination);
			sample.setMeshPrimProperties(destination, true, true);
			destination.setPoints(sample.points, time);
			destination.setBoundsAttr(sample.points, time);
			return true;
		}

		std::mutex m_evaluation_mutex;
		std::shared_ptr<NukeReaderSession> m_session;
	};

	explicit OpenVolumetricGeo(Node* node)
		: SourceGeomOp(node, BuildEngine<Engine>())
	{
	}

	const char* Class() const override
	{
		return kNodeClass;
	}

	const char* node_help() const override
	{
		return kNodeHelp;
	}

	void knobs(DD::Image::Knob_Callback callback) override
	{
		SourceGeomOp::knobs(callback);
		DD::Image::File_knob(callback, &m_file, kFileKnob, "OpenVolumetric MP4");
		KnobDefinesGeometry(callback);
		DD::Image::Int_knob(callback, &m_start_frame, kStartFrameKnob, "start frame");
		KnobDefinesGeometry(callback);
		DD::Image::Double_knob(
			callback, &m_frame_offset, kFrameOffsetKnob, "frame offset");
		KnobDefinesGeometry(callback);
		DD::Image::Double_knob(
			callback,
			&m_source_frame_rate,
			kSourceFrameRateKnob,
			"source frame rate");
		DD::Image::Tooltip(
			callback, "Use 0 to read the native frame rate from the MP4.");
		DD::Image::Double_knob(callback, &m_trim_out, kTrimOutKnob, "trim out");
		DD::Image::SetFlags(callback, DD::Image::Knob::HIDDEN);
		DD::Image::Int_knob(callback, &m_before_mode, kBeforeModeKnob, "before mode");
		DD::Image::SetFlags(callback, DD::Image::Knob::HIDDEN);
		DD::Image::Int_knob(callback, &m_after_mode, kAfterModeKnob, "after mode");
		DD::Image::SetFlags(callback, DD::Image::Knob::HIDDEN);
		KnobDefinesGeometry(callback);
		DD::Image::Double_knob(
			callback, &m_geometry_scale, kGeometryScaleKnob, "geometry scale");
		KnobModifiesAttribValues(callback);
		DD::Image::Int_knob(
			callback, &m_cache_frames, kCacheFramesKnob, "cache frames");
		KnobDefinesGeometry(callback);
		DD::Image::Int_knob(
			callback, &m_cache_megabytes, kCacheMegabytesKnob, "cache MB");
		KnobDefinesGeometry(callback);
		DD::Image::Int_knob(
			callback, &m_reload_version, kReloadVersionKnob, "reload version");
		DD::Image::SetFlags(callback, DD::Image::Knob::HIDDEN);
		KnobDefinesGeometry(callback);
		DD::Image::Divider(callback);
		makeTransformKnob(callback);
		DD::Image::Tab_knob(callback, 0, "Display");
		SourceGeomOp::addDisplayOptionsKnobs(callback);
	}

	void appendGeomState(DD::Image::Hash& operation_hash) override
	{
		SourceGeomOp::appendGeomState(operation_hash);
		DD::Image::Hash timeline_hash;
		timeline_hash.append(outputContext().frame());
		operation_hash.append(timeline_hash);
		// OpenVolumetric sequences may change their triangle indices at any frame.
		// Topology is a define-geometry concern in Nuke's scene contract; forcing
		// it through modify-values can transiently invalidate Hydra's material and
		// normal resources. Rebuild the complete primitive for the requested frame.
		appendToDefineGeometryTarget(timeline_hash);
	}

	static DD::Image::Op* build(Node* node)
	{
		return new OpenVolumetricGeo(node);
	}

private:
	// Nuke owns the storage referenced by file-knob string pointers.
	const char* m_file = nullptr;
	int m_start_frame = 1;
	double m_frame_offset = 0.0;
	double m_source_frame_rate = 0.0;
	double m_trim_out = -1.0;
	int m_before_mode = 1;
	int m_after_mode = 1;
	double m_geometry_scale = 1.0;
	int m_cache_frames = 8;
	int m_cache_megabytes = 256;
	int m_reload_version = 0;

	static const DD::Image::GeomOp::Description description_;
};

const DD::Image::GeomOp::Description OpenVolumetricGeo::description_(
	kNodeClass,
	OpenVolumetricGeo::build);
} // namespace
} // namespace openvolumetric::nuke
