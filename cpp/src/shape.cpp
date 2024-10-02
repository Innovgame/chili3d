#include "shared.hpp"
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepExtrema_ExtCC.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <GCPnts_AbscissaPoint.hxx>
#include <Geom_OffsetCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomAbs_JoinType.hxx>
#include <ShapeAnalysis.hxx>
#include <TopExp.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_CompSolid.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>


using namespace emscripten;

class Shape {
public:

    static bool isClosed(const TopoDS_Shape& shape) {
        return BRep_Tool::IsClosed(shape);
    }

    static ShapeArray findAncestor(const TopoDS_Shape& from, const TopoDS_Shape& subShape, const TopAbs_ShapeEnum& ancestorType) {
        TopTools_IndexedDataMapOfShapeListOfShape map;
        TopExp::MapShapesAndAncestors(from, subShape.ShapeType(), ancestorType, map);
        auto index = map.FindIndex(subShape);
        auto shapes = map.FindFromIndex(index);

        return ShapeArray(val::array(shapes.begin(), shapes.end()));
    }

    static ShapeArray findSubShapes(const TopoDS_Shape& shape, const TopAbs_ShapeEnum& shapeType) {
        TopTools_IndexedMapOfShape indexShape;
        TopExp::MapShapes(shape, shapeType, indexShape);

        return ShapeArray(val::array(indexShape.cbegin(), indexShape.cend()));
    }

    static TopoDS_Shape sectionSS(const TopoDS_Shape& shape, const TopoDS_Shape& otherShape) {
        BRepAlgoAPI_Section section(shape, otherShape);
        return section.Shape();
    }

    static TopoDS_Shape sectionSP(const TopoDS_Shape& shape, const gp_Pln& pln) {
        BRepAlgoAPI_Section section(shape, pln);
        return section.Shape();
    }

    static TopoDS_Shape splitByEdgeOrWires(const TopoDS_Shape& shape, const ShapeArray& splitters) {
        std::vector<TopoDS_Shape> shapeVector = vecFromJSArray<TopoDS_Shape>(splitters);
        TopTools_SequenceOfShape shapes;
        for (auto& s : shapeVector) {
            shapes.Append(s);
        }

        BRepFeat_SplitShape splitter(shape);
        splitter.Add(shapes);
        splitter.Build();
        return splitter.Shape();
    }

};

class Vertex {
public:
    static Vector3 point(const TopoDS_Vertex& vertex) {
        return Vector3::fromPnt(BRep_Tool::Pnt(vertex));
    }
};

class Edge {
public:
    static TopoDS_Edge fromCurve (const Geom_Curve* curve) {
        Handle_Geom_Curve handleCurve(curve);
        BRepBuilderAPI_MakeEdge builder(handleCurve);
        return builder.Edge();
    }

    static double curveLength(const TopoDS_Edge& edge) {
        BRepAdaptor_Curve curve(edge);
        return GCPnts_AbscissaPoint::Length(curve);
    }

    static Handle_Geom_TrimmedCurve curve(const TopoDS_Edge& edge) {
        double start(0.0), end(0.0);
        auto curve = BRep_Tool::Curve(edge, start, end);
        Handle_Geom_TrimmedCurve trimmedCurve = new Geom_TrimmedCurve(curve, start, end);
        return trimmedCurve;
    }

    static TopoDS_Edge trim(const TopoDS_Edge& edge, double start, double end) {
        double u1(0.0), u2(0.0);
        auto curve = BRep_Tool::Curve(edge, u1, u2);
        BRepBuilderAPI_MakeEdge builder(curve, start, end);
        return builder.Edge();
    }

    static TopoDS_Edge offset(const TopoDS_Edge& edge, const gp_Dir& dir, double offset) {
        double start(0.0), end(0.0);
        auto curve = BRep_Tool::Curve(edge, start, end);
        Handle_Geom_TrimmedCurve trimmedCurve = new Geom_TrimmedCurve(curve, start, end);
        Handle_Geom_OffsetCurve offsetCurve = new Geom_OffsetCurve(trimmedCurve, offset, dir);
        BRepBuilderAPI_MakeEdge builder(offsetCurve);
        return builder.Edge();
    }

    static PointAndParameterArray intersect(const TopoDS_Edge& edge, const TopoDS_Edge& otherEdge) {
        std::vector<PointAndParameter> points;
        BRepExtrema_ExtCC cc(edge, otherEdge);
        if (cc.IsDone() && cc.NbExt() > 0 && !cc.IsParallel()) {
            for (int i = 1; i <= cc.NbExt(); i++) {
                PointAndParameter pointAndParameter = {
                    Vector3::fromPnt(cc.PointOnE1(i)),
                    cc.ParameterOnE1(i),
                };
                points.push_back(pointAndParameter);
            }
        }

        return PointAndParameterArray(val::array(points));
    }


};

class Wire {
public:
    static TopoDS_Shape offset(const TopoDS_Wire& wire , double distance, const GeomAbs_JoinType& joinType) {
        BRepOffsetAPI_MakeOffset offsetter(wire, joinType);
        offsetter.Perform(distance);
        if (offsetter.IsDone()) {
            return offsetter.Shape();
        }
        return TopoDS_Shape();
    }

    static TopoDS_Face makeFace(const TopoDS_Wire& wire) {
        BRepBuilderAPI_MakeFace face(wire);
        return face.Face();
    }
};

class Face {
public:
    static TopoDS_Shape offset(const TopoDS_Face& face , double distance, const GeomAbs_JoinType& joinType) {
        BRepOffsetAPI_MakeOffset offsetter(face, joinType);
        offsetter.Perform(distance);
        if (offsetter.IsDone()) {
            return offsetter.Shape();
        }
        return TopoDS_Shape();
    }

    static Domain curveOnSurface(const TopoDS_Face& face, const TopoDS_Edge& edge) {
        double start(0.0), end(0.0);
        if (BRep_Tool::CurveOnSurface(edge, face, start, end).IsNull()) {
            return Domain();
        }
        Domain domain = { start, end };
        return domain;
    }

    static void normal(const TopoDS_Face& face, double u, double v, gp_Pnt& point, gp_Vec& normal) {
        BRepGProp_Face gpProp(face);
        gpProp.Normal(u, v, point, normal);
    }

    static TopoDS_Wire outerWire(const TopoDS_Face& face) {
        return ShapeAnalysis::OuterWire(face);
    }

    static Handle_Geom_Surface surface(const TopoDS_Face& face) {
        return BRep_Tool::Surface(face);
    }

};

EMSCRIPTEN_BINDINGS(Shape) {

    class_<Shape>("Shape")
        .class_function("findAncestor", &Shape::findAncestor)
        .class_function("findSubShapes", &Shape::findSubShapes)
        .class_function("sectionSS", &Shape::sectionSS)
        .class_function("sectionSP", &Shape::sectionSP)
        .class_function("isClosed", &Shape::isClosed)
        .class_function("splitByEdgeOrWires", &Shape::splitByEdgeOrWires)
    ;

    class_<Vertex>("Vertex")
        .class_function("point", &Vertex::point)
    ;

    class_<Edge>("Edge")
        .class_function("fromCurve", &Edge::fromCurve, allow_raw_pointers())
        .class_function("curve", &Edge::curve)
        .class_function("curveLength", &Edge::curveLength)
        .class_function("trim", &Edge::trim)
        .class_function("intersect", &Edge::intersect)
        .class_function("offset", &Edge::offset)
    ;

    class_<Wire>("Wire")
        .class_function("offset", &Wire::offset)
        .class_function("makeFace", &Wire::makeFace)
    ;

    class_<Face>("Face")
        .class_function("offset", &Face::offset)
        .class_function("outerWire", &Face::outerWire)
        .class_function("surface", &Face::surface)
        .class_function("normal", &Face::normal)
        .class_function("curveOnSurface", &Face::curveOnSurface)
    ;

}
