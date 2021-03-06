#ifndef SPLITTER_SOFTCUT_HPP
#define SPLITTER_SOFTCUT_HPP

#include "cut.hpp"

/*

Softcut Algorithm
 - walk over all node-versions
   - walk over all bboxes
     - if the current node-version is inside the bbox
       - record its id in the bboxes node-tracker

 - walk over all way-versions
   - walk over all bboxes
     - walk over all way-nodes
       - if the way-node is recorded in the bboxes node-tracker
         - record its id in the bboxes way-id-tracker

     - if its id is in the bboxes way-tracker
       - walk over all way-nodes
         - record its id in the bboxes extra-node-tracker

 - walk over all relation-versions
   - walk over all bboxes
     - walk over all relation-members
       - if the relation-member is recorded in the bboxes node- or way-tracker
         - record its id in the bboxes relation-tracker

Second Pass
 - walk over all node-versions
   - walk over all bboxes
     - if the node-id is recorded in the bboxes node-tracker or in the extra-node-tracker
       - send the node to the bboxes writer

 - walk over all way-versions
   - walk over all bboxes
     - if the way-id is recorded in the bboxes way-tracker
       - send the way to the bboxes writer

 - walk over all relation-versions
   - walk over all bboxes
     - if the relation-id is recorded in the bboxes relation-tracker
       - send the relation to the bboxes writer

features:
 - if an object is in the extract, all versions of it are there
 - ways and relations are not changed
 - ways are reference-complete

disadvantages
 - dual pass
 - needs more RAM: 350 MB per BBOX
   - ((1400000000÷8)+(1400000000÷8)+(130000000÷8)+(1500000÷8))÷1024÷1024 MB
 - relations will have dead references

*/


class SoftcutExtractInfo : public ExtractInfo {

public:
    growing_bitset node_tracker;
    growing_bitset extra_node_tracker;
    growing_bitset way_tracker;
    growing_bitset relation_tracker;

    SoftcutExtractInfo(std::string name) : ExtractInfo(name) {}
};

class SoftcutInfo : public CutInfo<SoftcutExtractInfo> {

public:
    std::multimap<osm_object_id_t, osm_object_id_t> cascading_relations_tracker;
};


class SoftcutPassOne : public Cut<SoftcutInfo> {

public:
    SoftcutPassOne(SoftcutInfo *info) : Cut<SoftcutInfo>(info) {}

    void init(Osmium::OSM::Meta& meta) {
        fprintf(stderr, "softcut first-pass init\n");
        for(int i = 0, l = info->extracts.size(); i<l; i++) {
            fprintf(stderr, "\textract[%d] %s\n", i, info->extracts[i]->name.c_str());
        }

        if(debug) {
            fprintf(stderr, "\n\n===== NODES =====\n\n");
        } else {
            pg.init(meta);
        }
    }

    // - walk over all node-versions
    //   - walk over all bboxes
    //     - if the current node-version is inside the bbox
    //       - record its id in the bboxes node-tracker
    void node(const shared_ptr<Osmium::OSM::Node const>& node) {
        if(debug) {
            fprintf(stderr, "softcut node %d v%d\n", node->id(), node->version());
        } else {
            pg.node(node);
        }

        for(int i = 0, l = info->extracts.size(); i<l; i++) {
            SoftcutExtractInfo *extract = info->extracts[i];
            if(extract->contains(node)) {
                if(debug) fprintf(stderr, "node is in extract [%d], recording in node_tracker\n", i);

                extract->node_tracker.set(node->id());
            }
        }
    }

    void after_nodes() {
        if(debug) {
            fprintf(stderr, "after nodes\n");
            fprintf(stderr, "\n\n===== WAYS =====\n\n");
        } else {
            pg.after_nodes();
        }
    }

    // - walk over all way-versions
    //   - walk over all bboxes
    //     - walk over all way-nodes
    //       - if the way-node is recorded in the bboxes node-tracker
    //         - record its id in the bboxes way-id-tracker
    //
    //     - if its id is in the bboxes way-tracker
    //       - walk over all way-nodes
    //         - record its id in the bboxes extra-node-tracker
    void way(const shared_ptr<Osmium::OSM::Way const>& way) {
        if(debug) {
            fprintf(stderr, "softcut way %d v%d\n", way->id(), way->version());
        } else {
            pg.way(way);
        }

        for(int i = 0, l = info->extracts.size(); i<l; i++) {
            bool hit = false;
            SoftcutExtractInfo *extract = info->extracts[i];
            Osmium::OSM::WayNodeList nodes = way->nodes();

            for(int ii = 0, ll = nodes.size(); ii<ll; ii++) {
                Osmium::OSM::WayNode node = nodes[ii];
                if(extract->node_tracker.get(node.ref())) {
                    if(debug) fprintf(stderr, "way has a node (%d) inside extract [%d], recording in way_tracker\n", node.ref(), i);
                    hit = true;

                    extract->way_tracker.set(way->id());
                    break;
                }
            }

            if(hit) {
                if(debug) fprintf(stderr, "also recording the extra nodes of the way in the extra_node_tracker: \n\t");
                for(int ii = 0, ll = nodes.size(); ii<ll; ii++) {
                    Osmium::OSM::WayNode node = nodes[ii];
                    if(debug) fprintf(stderr, "%d ", node.ref());

                    extract->extra_node_tracker.set(node.ref());
                }
                if(debug) fprintf(stderr, "\n");
            }
        }
    }

    void after_ways() {
        if(debug) {
            fprintf(stderr, "after ways\n");
            fprintf(stderr, "\n\n===== RELATIONS =====\n\n");
        }
        else {
            pg.after_ways();
        }
    }

    // - walk over all relation-versions
    //   - walk over all bboxes
    //     - walk over all relation-members
    //       - if the relation-member is recorded in the bboxes node- or way-tracker
    //         - record its id in the bboxes relation-tracker
    void relation(const shared_ptr<Osmium::OSM::Relation const>& relation) {
        if(debug) {
            fprintf(stderr, "softcut relation %d v%d\n", relation->id(), relation->version());
        } else {
            pg.relation(relation);
        }

        for(int i = 0, l = info->extracts.size(); i<l; i++) {
            bool hit = false;
            SoftcutExtractInfo *extract = info->extracts[i];
            Osmium::OSM::RelationMemberList members = relation->members();
            
            for(int ii = 0, ll = members.size(); ii<ll; ii++) {
                Osmium::OSM::RelationMember member = members[ii];

                if( !hit && (
                    (member.type() == 'n' && extract->node_tracker.get(member.ref())) ||
                    (member.type() == 'w' && extract->way_tracker.get(member.ref())) ||
                    (member.type() == 'r' && extract->relation_tracker.get(member.ref()))
                )) {

                    if(debug) fprintf(stderr, "relation has a member (%c %d) inside extract [%d], recording in relation_tracker\n", member.type(), member.ref(), i);
                    hit = true;

                    extract->relation_tracker.set(relation->id());
                }

                if(member.type() == 'r') {
                    if(debug) fprintf(stderr, "recording cascading-pair: %d -> %d\n", member.ref(), relation->id());
                    info->cascading_relations_tracker.insert(std::make_pair(member.ref(), relation->id()));
                }
            }

            if(hit) {
                cascading_relations(extract, relation->id());
            }
        }
    }

    void cascading_relations(SoftcutExtractInfo *extract, osm_object_id_t id) {
        typedef std::multimap<osm_object_id_t, osm_object_id_t>::const_iterator mm_iter;

        std::pair<mm_iter, mm_iter> r = info->cascading_relations_tracker.equal_range(id);
        if(r.first == r.second) {
            return;
        }

        for(mm_iter it = r.first; it !=r.second; ++it) {
            if(debug) fprintf(stderr, "\tcascading: %d\n", it->second);

            if(extract->relation_tracker.get(it->second))
                continue;

            extract->relation_tracker.set(it->second);

            cascading_relations(extract, it->second);
        }
    }

    void after_relations() {
        if(debug) {
            fprintf(stderr, "after relations\n");
        } else {
            pg.after_relations();
        }
    }

    void final() {
        if(!debug) {
            pg.final();
        }

        fprintf(stderr, "softcut first-pass finished\n");
    }
};





class SoftcutPassTwo : public Cut<SoftcutInfo> {

public:
    SoftcutPassTwo(SoftcutInfo *info) : Cut<SoftcutInfo>(info) {}

    void init(Osmium::OSM::Meta& meta) {
        fprintf(stderr, "softcut second-pass init\n");

        if(debug) {
            fprintf(stderr, "\n\n===== NODES =====\n\n");
        } else {
            pg.init(meta);
        }
    }

    // - walk over all node-versions
    //   - walk over all bboxes
    //     - if the node-id is recorded in the bboxes node-tracker or in the extra-node-tracker
    //       - send the node to the bboxes writer
    void node(const shared_ptr<Osmium::OSM::Node const>& node) {
        if(debug) {
            fprintf(stderr, "softcut node %d v%d\n", node->id(), node->version());
        } else {
            pg.node(node);
        }

        for(int i = 0, l = info->extracts.size(); i<l; i++) {
            SoftcutExtractInfo *extract = info->extracts[i];

            if(extract->node_tracker.get(node->id()) || extract->extra_node_tracker.get(node->id()))
                extract->writer->node(node);
        }
    }

    void after_nodes() {
        if(debug) {
            fprintf(stderr, "after nodes\n");
            fprintf(stderr, "\n\n===== WAYS =====\n\n");
        } else {
            pg.after_nodes();
        }
    }

    // - walk over all way-versions
    //   - walk over all bboxes
    //     - if the way-id is recorded in the bboxes way-tracker
    //       - send the way to the bboxes writer
    void way(const shared_ptr<Osmium::OSM::Way const>& way) {
        if(debug) {
            fprintf(stderr, "softcut way %d v%d\n", way->id(), way->version());
        } else {
            pg.way(way);
        }

        for(int i = 0, l = info->extracts.size(); i<l; i++) {
            SoftcutExtractInfo *extract = info->extracts[i];

            if(extract->way_tracker.get(way->id()))
                extract->writer->way(way);
        }
    }

    void after_ways() {
        if(debug) {
            fprintf(stderr, "after ways\n");
            fprintf(stderr, "\n\n===== RELATIONS =====\n\n");
        }
        else {
            pg.after_ways();
        }
    }

    // - walk over all relation-versions
    //   - walk over all bboxes
    //     - if the relation-id is recorded in the bboxes relation-tracker
    //       - send the relation to the bboxes writer
    void relation(const shared_ptr<Osmium::OSM::Relation const>& relation) {
        if(debug) {
            fprintf(stderr, "softcut relation %d v%d\n", relation->id(), relation->version());
        } else {
            pg.relation(relation);
        }

        for(int i = 0, l = info->extracts.size(); i<l; i++) {
            SoftcutExtractInfo *extract = info->extracts[i];

            if(extract->relation_tracker.get(relation->id()))
                extract->writer->relation(relation);
        }
    }

    void after_relations() {
        if(debug) {
            fprintf(stderr, "after relations\n");
        } else {
            pg.after_relations();
        }
    }

    void final() {
        if(!debug) {
            pg.final();
        }

        fprintf(stderr, "softcut second-pass finished\n");
    }
};

#endif // SPLITTER_SOFTCUT_HPP

